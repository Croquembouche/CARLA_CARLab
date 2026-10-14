// Independent fixture oracle from raw source alpha bytes, not native atlas.
#pragma once
namespace CarlaAlphaReference {
struct Source {
  int32 Width=0,Height=0;float Clip=0;bool ClampX=false,ClampY=false;TArray<uint8> Bytes;
  bool Load(const TSharedPtr<FJsonObject>& Row,const FString& Dir) {
    const auto O=Row->GetObjectField(TEXT("opacity"));Width=O->GetIntegerField(TEXT("width"));Height=O->GetIntegerField(TEXT("height"));Clip=O->GetNumberField(TEXT("clip_value"));ClampX=O->GetStringField(TEXT("address_x"))==TEXT("clamp");ClampY=O->GetStringField(TEXT("address_y"))==TEXT("clamp");
    if(!FFileHelper::LoadFileToArray(Bytes,*(Dir/O->GetStringField(TEXT("data_file"))))||Bytes.Num()!=Width*Height)return false;
    FSHAHash Hash;FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash.Hash);return Hash.ToString().Equals(O->GetStringField(TEXT("sha1")),ESearchCase::IgnoreCase);
  }
  float Alpha(const FVector2f& UV)const {
    const float U=(ClampX?FMath::Clamp(UV.X,0.f,1.f):UV.X-FMath::FloorToFloat(UV.X))*Width-.5f,V=(ClampY?FMath::Clamp(UV.Y,0.f,1.f):UV.Y-FMath::FloorToFloat(UV.Y))*Height-.5f;
    const int32 X=FMath::FloorToInt(U),Y=FMath::FloorToInt(V);const float FX=U-X,FY=V-Y;
    const auto At=[&](int32 A,int32 B){A=ClampX?FMath::Clamp(A,0,Width-1):(A%Width+Width)%Width;B=ClampY?FMath::Clamp(B,0,Height-1):(B%Height+Height)%Height;return float(Bytes[B*Width+A])/255.f;};
    return (At(X,Y)*(1-FX)+At(X+1,Y)*FX)*(1-FY)+(At(X,Y+1)*(1-FX)+At(X+1,Y+1)*FX)*FY;
  }
};
}
