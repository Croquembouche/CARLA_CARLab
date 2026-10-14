// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/LidarInfrared.h"
#include "Carla.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/SkeletalMeshRenderData.h"
#if WITH_EDITOR
#include "Rendering/SkeletalMeshModel.h"
#endif
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "HAL/IConsoleManager.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Serialization/Archive.h"
namespace CarlaLidarInfrared {
namespace {
struct Entry {
  FVector4f Low,High,Wet=FVector4f(1,1,1,1);
  FVector2f Wavelength=FVector2f(905,1550),Scale=FVector2f(1,1),Offset=FVector2f(0,0);
  uint32 Pixel=0,Width=0,Height=0,OpacityPixel=0,OpacityWidth=0,OpacityHeight=0;
  bool OpacityClampX=false,OpacityClampY=false;
  float OpacityClip=.333333f;FVector2f OpacityScale=FVector2f(1,1),OpacityOffset=FVector2f(0,0);
};
TArray<Entry> Entries;TMap<FString,int32> Names;TArray<FVector4f> Atlas;
bool Ready=false;
uint64 RegistryRevision=0;
FString RegistryPath,RegistryHash,RegistryCalibration=TEXT("unknown"),ResourceHash;
int32 ResourceCount=0;
TArray<FString> ProfileNames;
bool AlphaResource(const FString& Registry,const TSharedPtr<FJsonObject>& Opacity,int32 Pixels,TArray<uint8>& Data,FString& Key,FString& Error) {
  FString Relative,Digest;
  if(!Opacity->TryGetStringField(TEXT("data_file"),Relative)||Relative.IsEmpty()||!FPaths::IsRelative(Relative)||
     !Opacity->TryGetStringField(TEXT("sha1"),Digest)||Digest.Len()!=40) {Error=TEXT("Alpha8 requires a relative data_file and40hex sha1.");return false;}
  for(TCHAR C:Digest)if(!FChar::IsHexDigit(C)){Error=TEXT("Invalid alpha8 SHA1.");return false;}
  const FString Directory=FPaths::GetPath(FPaths::ConvertRelativePathToFull(Registry));
  FString Full=FPaths::ConvertRelativePathToFull(Directory,Relative);FPaths::NormalizeFilename(Full);
  if(!FPaths::CollapseRelativeDirectories(Full)||!FPaths::IsUnderDirectory(Full,Directory)){Error=TEXT("Alpha8 data_file must remain under its registry directory.");return false;}
  // Size is checked before allocation/read. Hash and parse use the same
  // immutable bytes, including when the underlying file subsequently changes.
  TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Full));
  if(!Reader||Reader->TotalSize()!=Pixels){Error=TEXT("Alpha8 missing or exact byte count mismatch: ")+Relative;return false;}
  Data.SetNumUninitialized(Pixels);Reader->Serialize(Data.GetData(),Pixels);
  if(Reader->IsError()||Reader->TotalSize()!=Pixels){Error=TEXT("Alpha8 read failed: ")+Relative;return false;}
  FSHAHash Actual;FSHA1::HashBuffer(Data.GetData(),Data.Num(),Actual.Hash);
  if(!Actual.ToString().Equals(Digest,ESearchCase::IgnoreCase)){Error=TEXT("Alpha8 immutable byte hash mismatch: ")+Relative;return false;}
  Key=Full+TEXT("|")+Actual.ToString()+FString::Printf(TEXT("|%d"),Pixels);return true;
}
bool Vector(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,float* Out,int N,float Min,float Max,bool Required=true) {
  const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O->TryGetArrayField(Key,A))return !Required&&!O->HasField(Key);
  if(A->Num()!=N)return false;
  for(int I=0;I<N;++I) { double X;if(!(*A)[I]->TryGetNumber(X)||!FMath::IsFinite(X)||X<Min||X>Max)return false;Out[I]=X; }return true;
}
float Bits(uint32 X) { float F;FMemory::Memcpy(&F,&X,4);return F; }
void TriangleUVs(UPrimitiveComponent* C,uint32 Section,TArray<FVector4f>& Out) {
  if(auto* S=Cast<UStaticMeshComponent>(C)) {
    UStaticMesh* Mesh=S->GetStaticMesh();if(!Mesh||!Mesh->GetRenderData()||Mesh->GetRenderData()->LODResources.IsEmpty())return;
    const auto& L=Mesh->GetRenderData()->LODResources[0];if(!L.Sections.IsValidIndex(Section)||!L.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords())return;
    const auto I=L.IndexBuffer.GetArrayView();const auto& Part=L.Sections[Section];
    for(uint32 T=0;T<Part.NumTriangles;++T)for(uint32 V=0;V<3;++V) {
      const uint32 K=Part.FirstIndex+T*3+V;if(K>=uint32(I.Num()))return;
      const auto UV=L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K],0);Out.Add(FVector4f(UV.X,UV.Y,0,0));
    }
  } else if(auto* S=Cast<USkinnedMeshComponent>(C)) {
    auto* Mesh=Cast<USkeletalMesh>(S->GetSkinnedAsset());if(!Mesh||!Mesh->GetResourceForRendering()||Mesh->GetResourceForRendering()->LODRenderData.IsEmpty())return;
    const auto& L=Mesh->GetResourceForRendering()->LODRenderData[0];if(!L.RenderSections.IsValidIndex(Section)||!L.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords())return;
    const auto* I=L.MultiSizeIndexContainer.GetIndexBuffer();if(!I)return;const auto& Part=L.RenderSections[Section];
    for(uint32 T=0;T<Part.NumTriangles;++T)for(uint32 V=0;V<3;++V) {
      const auto UV=L.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I->Get(Part.BaseIndex+T*3+V),0);Out.Add(FVector4f(UV.X,UV.Y,0,0));
    }
  }
}
bool HitUV(const FHitResult& H,FVector2f& UV) {
  auto* C=Cast<UStaticMeshComponent>(H.GetComponent());if(!C||!C->GetStaticMesh()||!C->GetStaticMesh()->GetRenderData()||C->GetStaticMesh()->GetRenderData()->LODResources.IsEmpty()||H.FaceIndex<0)return false;
  const auto& L=C->GetStaticMesh()->GetRenderData()->LODResources[0];const auto I=L.IndexBuffer.GetArrayView();
  const uint32 K=uint32(H.FaceIndex)*3;if(K+2>=uint32(I.Num())||!L.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords())return false;
  const FVector A(L.VertexBuffers.PositionVertexBuffer.VertexPosition(I[K])),B(L.VertexBuffers.PositionVertexBuffer.VertexPosition(I[K+1])),D(L.VertexBuffers.PositionVertexBuffer.VertexPosition(I[K+2]));
  FTransform Transform=C->GetComponentTransform();
  if(auto* Instances=Cast<UInstancedStaticMeshComponent>(C))
    if(!Instances->GetInstanceTransform(H.Item,Transform,true))return false;
  const FVector P=Transform.InverseTransformPosition(H.ImpactPoint),E=B-A,F=D-A,Q=P-A;
  const double EE=E|E,EF=E|F,FF=F|F,Det=EE*FF-EF*EF;if(FMath::Abs(Det)<1.e-12)return false;
  const double V=((Q|E)*FF-(Q|F)*EF)/Det,W=((Q|F)*EE-(Q|E)*EF)/Det;
  UV=L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K],0)*float(1-V-W)+L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K+1],0)*float(V)+L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K+2],0)*float(W);return true;
}
}
bool Reload(const FString& Path,FString& Error) {
  check(IsInGameThread());Error.Reset();
  // The first initialization cannot invalidate any previous IR snapshot.
  // Later reloads must wait for all native frame consumers and delayed delivery.
  if(Ready&&CarlaGpuSensors::HasPendingFrames()) { Error=TEXT("Native sensor frames are pending; finish delivery before reloading infrared materials.");return false; }
  TArray<uint8> Bytes;FString Text;
  if(!FFileHelper::LoadFileToArray(Bytes,*Path)) { Error=TEXT("Cannot read infrared material registry: ")+Path;return false; }
  if(Bytes.Num()>64*1024*1024) { Error=TEXT("Infrared registry exceeds64MiB.");return false; }
  // Decode and hash one immutable byte array, so provenance describes exactly
  // the registry parsed even if the file is edited after this read.
  FFileHelper::BufferToString(Text,Bytes.GetData(),Bytes.Num());
  FSHAHash Hash;FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash.Hash);
  TSharedPtr<FJsonObject> Root;const auto Reader=TJsonReaderFactory<>::Create(Text);
  const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
  double Version=0;
  if(!FJsonSerializer::Deserialize(Reader,Root)||!Root.IsValid()||!Root->TryGetNumberField(TEXT("version"),Version)||Version!=1||!Root->TryGetArrayField(TEXT("materials"),Rows)||Rows->Num()>256) {
    Error=TEXT("Invalid infrared registry root (version1 and at most256 material rows required).");return false;
  }
  TArray<Entry> NewEntries;TMap<FString,int32> NewNames;TArray<FVector4f> NewAtlas;TArray<FString> NewProfileNames;TMap<FString,uint32> ResourceOffsets;TArray<FString> ResourceKeys;
  for(const auto& Row:*Rows) {
    const auto O=Row.IsValid() && Row->Type==EJson::Object?Row->AsObject():nullptr;Entry E;FString Name;
    bool Valid=O.IsValid()&&O->TryGetStringField(TEXT("name"),Name)&&!Name.IsEmpty()&&!NewNames.Contains(Name)&&
      Vector(O,TEXT("wavelengths_nm"),&E.Wavelength.X,2,200,3000,false)&&E.Wavelength.Y>E.Wavelength.X&&
      Vector(O,TEXT("dry_low"),&E.Low.X,4,0,1)&&Vector(O,TEXT("dry_high"),&E.High.X,4,0,1)&&
      Vector(O,TEXT("wet_multipliers"),&E.Wet.X,4,0,4,false)&&Vector(O,TEXT("uv_scale"),&E.Scale.X,2,-10000,10000,false)&&Vector(O,TEXT("uv_offset"),&E.Offset.X,2,-10000,10000,false);
    const TSharedPtr<FJsonObject>* Mask=nullptr;TArray<FVector4f> NewPixels;
    if(Valid&&O->HasField(TEXT("mask"))&&!O->TryGetObjectField(TEXT("mask"),Mask))Valid=false;
    if(Valid&&O->TryGetObjectField(TEXT("mask"),Mask)) {
      double W=0,H=0;const TArray<TSharedPtr<FJsonValue>>* RGBA=nullptr;
      Valid=(*Mask)->TryGetNumberField(TEXT("width"),W)&&(*Mask)->TryGetNumberField(TEXT("height"),H)&&W>=1&&H>=1&&W<=256&&H<=256&&W==std::floor(W)&&H==std::floor(H)&&(*Mask)->TryGetArrayField(TEXT("rgba"),RGBA)&&RGBA->Num()==int(W*H*4);
      if(Valid) { E.Width=W;E.Height=H;E.Pixel=NewAtlas.Num();NewPixels.SetNum(E.Width*E.Height);
        for(int I=0;I<RGBA->Num();++I) { double V;if(!(*RGBA)[I]->TryGetNumber(V)||!FMath::IsFinite(V)||V<0||V>1) {Valid=false;break;}(&NewPixels[I/4].X)[I%4]=V; }
      }
    }
    const TSharedPtr<FJsonObject>* Opacity=nullptr;TArray<FVector4f> NewOpacity;
    if(Valid&&O->HasField(TEXT("opacity"))&&!O->TryGetObjectField(TEXT("opacity"),Opacity))Valid=false;
    if(Valid&&O->TryGetObjectField(TEXT("opacity"),Opacity)) {
      double W=0,H=0,Clip=0;const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
      Valid=(*Opacity)->TryGetNumberField(TEXT("width"),W)&&(*Opacity)->TryGetNumberField(TEXT("height"),H)&&W>=1&&H>=1&&W<=4096&&H<=4096&&W==std::floor(W)&&H==std::floor(H)&&
        (*Opacity)->TryGetNumberField(TEXT("clip_value"),Clip)&&FMath::IsFinite(Clip)&&Clip>=0&&Clip<=1&&
        Vector(*Opacity,TEXT("uv_scale"),&E.OpacityScale.X,2,-10000,10000,false)&&Vector(*Opacity,TEXT("uv_offset"),&E.OpacityOffset.X,2,-10000,10000,false)&&
        double(NewAtlas.Num())+NewPixels.Num()<=16777216;
      FString AddressX=TEXT("wrap"),AddressY=TEXT("wrap");
      if(Valid&&(*Opacity)->HasField(TEXT("address_x")))Valid=(*Opacity)->TryGetStringField(TEXT("address_x"),AddressX);
      if(Valid&&(*Opacity)->HasField(TEXT("address_y")))Valid=(*Opacity)->TryGetStringField(TEXT("address_y"),AddressY);
      Valid=Valid&&(AddressX==TEXT("wrap")||AddressX==TEXT("clamp"))&&(AddressY==TEXT("wrap")||AddressY==TEXT("clamp"));
      if(Valid) {
        E.OpacityClampX=AddressX==TEXT("clamp");E.OpacityClampY=AddressY==TEXT("clamp");
        E.OpacityWidth=W;E.OpacityHeight=H;E.OpacityPixel=NewAtlas.Num()+NewPixels.Num();E.OpacityClip=Clip;
        const bool Inline=(*Opacity)->HasField(TEXT("values")),Binary=(*Opacity)->HasField(TEXT("data_file"));
        Valid=Inline!=Binary;
        if(Valid&&Inline) {
          Valid=!(*Opacity)->HasField(TEXT("sha1"))&&(*Opacity)->TryGetArrayField(TEXT("values"),Values)&&Values->Num()==int(W*H)&&double(E.OpacityPixel)+W*H<=16777216;
          if(Valid){NewOpacity.SetNum(E.OpacityWidth*E.OpacityHeight);for(int I=0;I<Values->Num();++I) {double V;if(!(*Values)[I]->TryGetNumber(V)||!FMath::IsFinite(V)||V<0||V>1){Valid=false;break;}NewOpacity[I]=FVector4f(V,0,0,0);}}
        } else if(Valid) {
          TArray<uint8> Alpha;FString Key;
          if(!AlphaResource(Path,*Opacity,int(W*H),Alpha,Key,Error))return false;
          Key+=FString::Printf(TEXT("|%dx%d"),E.OpacityWidth,E.OpacityHeight);
          if(const uint32* Offset=ResourceOffsets.Find(Key))E.OpacityPixel=*Offset;
          else {
            Valid=double(E.OpacityPixel)+W*H<=16777216;
            if(Valid){NewOpacity.SetNum(Alpha.Num());for(int I=0;I<Alpha.Num();++I)NewOpacity[I]=FVector4f(float(Alpha[I])/255.f,0,0,0);ResourceOffsets.Add(Key,E.OpacityPixel);ResourceKeys.Add(Key);}
          }
        }
      }
    }
    if(!Valid) { Error=TEXT("Invalid or duplicate infrared material entry: ")+Name;return false; }
    NewAtlas.Append(NewPixels);NewAtlas.Append(NewOpacity);NewNames.Add(Name,NewEntries.Add(E));NewProfileNames.Add(Name);
  }
  if(NewAtlas.IsEmpty())NewAtlas.Add(FVector4f(1,1,1,1));
  FString Calibration=TEXT("unknown");const TSharedPtr<FJsonObject>* Metadata=nullptr;
  if(Root->TryGetObjectField(TEXT("calibration"),Metadata))(*Metadata)->TryGetStringField(TEXT("status"),Calibration);
  ResourceKeys.Sort();FString ResourceManifest=FString::Join(ResourceKeys,TEXT("\n"));FTCHARToUTF8 ResourceUtf8(*ResourceManifest);FSHAHash Resources;FSHA1::HashBuffer(ResourceUtf8.Get(),ResourceUtf8.Length(),Resources.Hash);
  Entries=MoveTemp(NewEntries);Names=MoveTemp(NewNames);Atlas=MoveTemp(NewAtlas);ProfileNames=MoveTemp(NewProfileNames);ResourceCount=ResourceKeys.Num();ResourceHash=Resources.ToString();
  RegistryPath=FPaths::ConvertRelativePathToFull(Path);RegistryHash=Hash.ToString();RegistryCalibration=Calibration;
  Ready=true;++RegistryRevision;
  UE_LOG(LogCarla,Display,TEXT("LIDAR_IR_REGISTRY revision=%llu profiles=%d sha1=%s calibration=%s path=%s"),RegistryRevision,Entries.Num(),*RegistryHash,*RegistryCalibration,*RegistryPath);
  return true;
}
void Prepare() {
  if(Ready)return;check(IsInGameThread());
  FString Error;
  if(!Reload(FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json"),Error)) {
    // Startup fallback remains explicit; failed later reloads retain a valid
    // registry, including its profile indices, atlas and provenance.
    Ready=true;Atlas.Add(FVector4f(1,1,1,1));++RegistryRevision;
    UE_LOG(LogCarla,Warning,TEXT("LIDAR_IR_REGISTRY fallback: %s"),*Error);
  }
}
uint64 Revision() { return RegistryRevision; }
FString ProfileName(int32 Profile) { return ProfileNames.IsValidIndex(Profile)?ProfileNames[Profile]:TEXT("fallback"); }
FString Status() {
  return FString::Printf(TEXT("revision=%llu profiles=%d sha1=%s resources=%d resources_sha1=%s calibration=%s path=%s"),RegistryRevision,Entries.Num(),*RegistryHash,ResourceCount,*ResourceHash,*RegistryCalibration,*RegistryPath);
}
int32 Find(UPrimitiveComponent* C,UMaterialInterface* M) {
  if(!Ready) { if(IsInGameThread())Prepare();else return -1; }
  if(C)for(const auto& Tag:C->ComponentTags) { FString Key;if(Tag.ToString().Split(TEXT("LidarIR="),nullptr,&Key))if(const int32* I=Names.Find(Key))return *I; }
  if(M) { if(const int32* I=Names.Find(M->GetPathName()))return *I;if(const int32* I=Names.Find(M->GetName()))return *I; }return -1;
}
CarlaLidarOptics::Material Sample(CarlaLidarOptics::Material Base,const FHitResult& H,float Nm,float Wetness,const FVector2f* MotionUV) {
  if(!Entries.IsValidIndex(Base.infrared))return Base;const auto& E=Entries[Base.infrared];
  const float A=FMath::Clamp((Nm-E.Wavelength.X)/(E.Wavelength.Y-E.Wavelength.X),0.f,1.f),W=FMath::Clamp(Wetness,0.f,1.f);
  FVector4f Value=FMath::Lerp(E.Low,E.High,A);float* V=&Value.X;const float* Wet=&E.Wet.X;for(int I=0;I<4;++I)V[I]=FMath::Clamp(V[I]*FMath::Lerp(1.f,Wet[I],W),0.f,1.f);
  FVector2f UV;const bool HasUV=MotionUV?(UV=*MotionUV,true):HitUV(H,UV);
  if(E.Width&&HasUV) {
    UV=UV*E.Scale+E.Offset;const uint32 X=FMath::Min(E.Width-1,uint32(FMath::FloorToInt((UV.X-FMath::FloorToFloat(UV.X))*E.Width))),Y=FMath::Min(E.Height-1,uint32(FMath::FloorToInt((UV.Y-FMath::FloorToFloat(UV.Y))*E.Height)));
    const auto Pixel=Atlas[E.Pixel+Y*E.Width+X];for(int I=0;I<4;++I)V[I]*=(&Pixel.X)[I];
  }
  Base.diffuse=Value.X;Base.retro=Value.Y;Base.specular=Value.Z;Base.transmission=Value.W;return Base;
}
bool HasOpacity(int32 Profile) {return Entries.IsValidIndex(Profile)&&Entries[Profile].OpacityWidth>0;}
bool AllowsHit(int32 Profile,const FVector2f& InputUV) {
  if(!HasOpacity(Profile))return true;const auto& E=Entries[Profile];
  const FVector2f UV=InputUV*E.OpacityScale+E.OpacityOffset;
  const float X=(E.OpacityClampX?FMath::Clamp(UV.X,0.f,1.f):UV.X-FMath::FloorToFloat(UV.X))*E.OpacityWidth-.5f,Y=(E.OpacityClampY?FMath::Clamp(UV.Y,0.f,1.f):UV.Y-FMath::FloorToFloat(UV.Y))*E.OpacityHeight-.5f;
  const int32 IX=FMath::FloorToInt(X),IY=FMath::FloorToInt(Y);const float FX=X-IX,FY=Y-IY;
  auto Pixel=[&](int32 PX,int32 PY){const int32 W=E.OpacityWidth,H=E.OpacityHeight;const int32 IX=E.OpacityClampX?FMath::Clamp(PX,0,W-1):(PX%W+W)%W,IY=E.OpacityClampY?FMath::Clamp(PY,0,H-1):(PY%H+H)%H;return Atlas[E.OpacityPixel+IY*W+IX].X;};
  const float Value=FMath::Lerp(FMath::Lerp(Pixel(IX,IY),Pixel(IX+1,IY),FX),FMath::Lerp(Pixel(IX,IY+1),Pixel(IX+1,IY+1),FX),FY);
  return Value>=E.OpacityClip;
}
GeometryKey Key(UPrimitiveComponent* C,uint32 Section) {
  GeometryKey K;K.Section=Section;
  if(auto* S=Cast<UStaticMeshComponent>(C)) {
    UStaticMesh* M=S->GetStaticMesh();K.Mesh=M;
    if(M && M->GetRenderData() && !M->GetRenderData()->LODResources.IsEmpty()) {
      const auto& L=M->GetRenderData()->LODResources[0];K.Render=&L;K.Guid=M->GetLightingGuid();
      K.Texcoords=L.VertexBuffers.StaticMeshVertexBuffer.GetTexCoordData();K.Vertices=L.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();K.Indices=L.IndexBuffer.IndexBufferRHI.GetReference();
      if(L.Sections.IsValidIndex(Section)){K.Triangles=L.Sections[Section].NumTriangles;K.FirstIndex=L.Sections[Section].FirstIndex;}
    }
  } else if(auto* S=Cast<USkinnedMeshComponent>(C)) {
    auto* M=Cast<USkeletalMesh>(S->GetSkinnedAsset());K.Mesh=M;
#if WITH_EDITOR
    if(M && M->GetImportedModel())K.Guid=M->GetImportedModel()->SkeletalMeshModelGUID;
#endif
    if(M && M->GetResourceForRendering() && !M->GetResourceForRendering()->LODRenderData.IsEmpty()) {
      const auto& L=M->GetResourceForRendering()->LODRenderData[0];K.Render=&L;
      K.Texcoords=L.StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordData();K.Vertices=L.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices();K.Indices=L.MultiSizeIndexContainer.GetIndexBuffer();
      if(L.RenderSections.IsValidIndex(Section)){K.Triangles=L.RenderSections[Section].NumTriangles;K.FirstIndex=L.RenderSections[Section].BaseIndex;}
    }
  }
  return K;
}
void Append(UPrimitiveComponent* C,uint32 Section,int32 Profile,TArray<FVector4f>& Info,TArray<FVector4f>& UVs,SnapshotUVCache* Cache) {
  if(!Entries.IsValidIndex(Profile)) { Info.AddZeroed(8);return; }const auto& E=Entries[Profile];
  uint32 Start=UVs.Num(),Triangles=0;
  if(E.Width||E.OpacityWidth) {
    const UObject* Mesh=nullptr;
    if(auto* Static=Cast<UStaticMeshComponent>(C))Mesh=Static->GetStaticMesh();
    else if(auto* Skinned=Cast<USkinnedMeshComponent>(C))Mesh=Skinned->GetSkinnedAsset();
    const TPair<const UObject*,uint32> Key(Mesh,Section);
    const FIntPoint* Existing=Cache && Mesh ? Cache->Find(Key) : nullptr;
    if(Existing) { Start=Existing->X;Triangles=Existing->Y; }
    else {
      TriangleUVs(C,Section,UVs);Triangles=(UVs.Num()-Start)/3;
      if(Cache && Mesh)Cache->Add(Key,FIntPoint(Start,Triangles));
    }
  }
  Info.Add(E.Low);Info.Add(E.High);Info.Add(E.Wet);Info.Add(FVector4f(E.Wavelength.X,E.Wavelength.Y,E.Scale.X,E.Scale.Y));
  Info.Add(FVector4f(E.Offset.X,E.Offset.Y,Bits(E.Pixel),Bits(E.Width)));
  Info.Add(FVector4f(Bits(E.Height),Bits(Start),Bits(Triangles),float(1u|(E.OpacityClampX?2u:0u)|(E.OpacityClampY?4u:0u))));
  Info.Add(FVector4f(Bits(E.OpacityPixel),Bits(E.OpacityWidth),Bits(E.OpacityHeight),E.OpacityClip));
  Info.Add(FVector4f(E.OpacityScale.X,E.OpacityScale.Y,E.OpacityOffset.X,E.OpacityOffset.Y));
}
const TArray<FVector4f>& Pixels() { return Atlas; }
namespace {
FAutoConsoleCommand ReloadCommand(TEXT("carla.Sensors.ReloadInfrared"),TEXT("Reload explicit infrared profiles after all native sensor frames complete; optional registry path."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
      if(Args.Num()>1) { UE_LOG(LogCarla,Warning,TEXT("LIDAR_IR_RELOAD rejected: expected zero or one registry path."));return; }
      FString Error;const FString Path=Args.IsEmpty()?FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json"):Args[0];
      if(!Reload(Path,Error))UE_LOG(LogCarla,Warning,TEXT("LIDAR_IR_RELOAD rejected: %s; retained %s"),*Error,*Status());
    }));
FAutoConsoleCommand StatusCommand(TEXT("carla.Sensors.InfraredStatus"),TEXT("Report the loaded native infrared registry revision, exact-byte hash and calibration status."),
    FConsoleCommandDelegate::CreateLambda([] { Prepare();UE_LOG(LogCarla,Display,TEXT("LIDAR_IR_STATUS %s"),*Status()); }));
}
}
