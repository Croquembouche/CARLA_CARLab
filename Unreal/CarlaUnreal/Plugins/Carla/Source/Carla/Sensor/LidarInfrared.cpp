// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/LidarInfrared.h"
#include "Carla.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
namespace CarlaLidarInfrared {
namespace {
struct Entry {
  FVector4f Low,High,Wet=FVector4f(1,1,1,1);
  FVector2f Wavelength=FVector2f(905,1550),Scale=FVector2f(1,1),Offset=FVector2f(0,0);
  uint32 Pixel=0,Width=0,Height=0;
};
TArray<Entry> Entries;TMap<FString,int32> Names;TArray<FVector4f> Atlas;
bool Ready=false;
bool Vector(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,float* Out,int N,float Min,float Max,bool Required=true) {
  const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O->TryGetArrayField(Key,A))return !Required;
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
  const FVector P=C->GetComponentTransform().InverseTransformPosition(H.ImpactPoint),E=B-A,F=D-A,Q=P-A;
  const double EE=E|E,EF=E|F,FF=F|F,Det=EE*FF-EF*EF;if(FMath::Abs(Det)<1.e-12)return false;
  const double V=((Q|E)*FF-(Q|F)*EF)/Det,W=((Q|F)*EE-(Q|E)*EF)/Det;
  UV=L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K],0)*float(1-V-W)+L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K+1],0)*float(V)+L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K+2],0)*float(W);return true;
}
}
void Prepare() {
  if(Ready)return;check(IsInGameThread());Ready=true;
  const FString Path=FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json");FString Text;
  if(!FFileHelper::LoadFileToString(Text,*Path)) { Atlas.Add(FVector4f(1,1,1,1));return; }
  TSharedPtr<FJsonObject> Root;const auto Reader=TJsonReaderFactory<>::Create(Text);
  const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
  if(!FJsonSerializer::Deserialize(Reader,Root)||!Root.IsValid()||Root->GetIntegerField(TEXT("version"))!=1||!Root->TryGetArrayField(TEXT("materials"),Rows)||Rows->Num()>256) {
    UE_LOG(LogCarla,Error,TEXT("Invalid LiDAR infrared registry: %s"),*Path);Atlas.Add(FVector4f(1,1,1,1));return;
  }
  for(const auto& Row:*Rows) {
    const auto O=Row->AsObject();Entry E;FString Name;
    bool Valid=O.IsValid()&&O->TryGetStringField(TEXT("name"),Name)&&!Name.IsEmpty()&&!Names.Contains(Name)&&
      Vector(O,TEXT("wavelengths_nm"),&E.Wavelength.X,2,200,3000,false)&&E.Wavelength.Y>E.Wavelength.X&&
      Vector(O,TEXT("dry_low"),&E.Low.X,4,0,1)&&Vector(O,TEXT("dry_high"),&E.High.X,4,0,1)&&
      Vector(O,TEXT("wet_multipliers"),&E.Wet.X,4,0,4,false)&&Vector(O,TEXT("uv_scale"),&E.Scale.X,2,-10000,10000,false)&&Vector(O,TEXT("uv_offset"),&E.Offset.X,2,-10000,10000,false);
    const TSharedPtr<FJsonObject>* Mask=nullptr;TArray<FVector4f> NewPixels;
    if(Valid&&O->TryGetObjectField(TEXT("mask"),Mask)) {
      double W=0,H=0;const TArray<TSharedPtr<FJsonValue>>* RGBA=nullptr;
      Valid=(*Mask)->TryGetNumberField(TEXT("width"),W)&&(*Mask)->TryGetNumberField(TEXT("height"),H)&&W>=1&&H>=1&&W<=256&&H<=256&&W==std::floor(W)&&H==std::floor(H)&&(*Mask)->TryGetArrayField(TEXT("rgba"),RGBA)&&RGBA->Num()==int(W*H*4);
      if(Valid) { E.Width=W;E.Height=H;E.Pixel=Atlas.Num();NewPixels.SetNum(E.Width*E.Height);
        for(int I=0;I<RGBA->Num();++I) { double V;if(!(*RGBA)[I]->TryGetNumber(V)||!FMath::IsFinite(V)||V<0||V>1) {Valid=false;break;}(&NewPixels[I/4].X)[I%4]=V; }
      }
    }
    if(!Valid) { UE_LOG(LogCarla,Error,TEXT("Ignoring invalid infrared material entry: %s"),*Name);continue; }
    Atlas.Append(NewPixels);Names.Add(Name,Entries.Add(E));
  }
  if(Atlas.IsEmpty())Atlas.Add(FVector4f(1,1,1,1));
  UE_LOG(LogCarla,Display,TEXT("Loaded %d explicit infrared material profiles (authored values; calibration provenance remains in materials.json)"),Entries.Num());
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
void Append(UPrimitiveComponent* C,uint32 Section,int32 Profile,TArray<FVector4f>& Info,TArray<FVector4f>& UVs) {
  if(!Entries.IsValidIndex(Profile)) { Info.AddZeroed(6);return; }const auto& E=Entries[Profile];
  const uint32 Start=UVs.Num();if(E.Width)TriangleUVs(C,Section,UVs);
  Info.Add(E.Low);Info.Add(E.High);Info.Add(E.Wet);Info.Add(FVector4f(E.Wavelength.X,E.Wavelength.Y,E.Scale.X,E.Scale.Y));
  Info.Add(FVector4f(E.Offset.X,E.Offset.Y,Bits(E.Pixel),Bits(E.Width)));
  Info.Add(FVector4f(Bits(E.Height),Bits(Start),Bits((UVs.Num()-Start)/3),1));
}
const TArray<FVector4f>& Pixels() { return Atlas; }
}
