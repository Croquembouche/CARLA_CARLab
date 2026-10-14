// Editor-only read-only diagnostic. No world, shader edits or package saves.
#if WITH_EDITOR
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
namespace CarlaMaterialGraphAudit {
static TSharedPtr<FJsonObject> Pin(const FExpressionInput* Input) {
  auto Row=MakeShared<FJsonObject>();
  if(Input){Row->SetStringField(TEXT("source"),GetPathNameSafe(Input->Expression));Row->SetNumberField(TEXT("output_index"),Input->OutputIndex);
    if(Input->Expression&&Input->Expression->Outputs.IsValidIndex(Input->OutputIndex))
      Row->SetStringField(TEXT("output_name"),Input->Expression->Outputs[Input->OutputIndex].OutputName.ToString());
    Row->SetNumberField(TEXT("mask"),Input->Mask);Row->SetNumberField(TEXT("mask_r"),Input->MaskR);Row->SetNumberField(TEXT("mask_g"),Input->MaskG);Row->SetNumberField(TEXT("mask_b"),Input->MaskB);Row->SetNumberField(TEXT("mask_a"),Input->MaskA);
  }return Row;
}
static void Run(const TArray<FString>& Args) {
  const FString Owned=TEXT("/mnt/simulations/town-realism/Town02/native-validation382/CarlaUnreal/");
  const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
  if(Root!=Owned||Args.Num()<2||!Args[0].StartsWith(TEXT("/mnt/simulations/town-realism/Town02/reports/materials393/"))||Args[0].Contains(TEXT(".."))){UE_LOG(LogCarla,Error,TEXT("WEATHER_GRAPH393_FAILED owned_scope"));return;}
  TArray<TSharedPtr<FJsonValue>> Materials,Nodes;TSet<UMaterialExpression*> Seen;TArray<UMaterialExpression*> Queue;
  for(int32 A=1;A<Args.Num();++A){
    if(!Args[A].StartsWith(TEXT("/Game/TownRealism/Town02/Materials/"))){UE_LOG(LogCarla,Error,TEXT("WEATHER_GRAPH393_FAILED asset_scope"));return;}
    auto* Interface=LoadObject<UMaterialInterface>(nullptr,*Args[A]);auto* M=Interface?Interface->GetMaterial():nullptr;
    if(!M){UE_LOG(LogCarla,Error,TEXT("WEATHER_GRAPH393_FAILED load %s"),*Args[A]);return;}
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("instance"),Interface->GetPathName());Row->SetStringField(TEXT("master"),M->GetPathName());Row->SetBoolField(TEXT("use_material_attributes"),M->bUseMaterialAttributes);
    auto Roots=MakeShared<FJsonObject>();
    for(const auto& Entry:TArray<TPair<FString,EMaterialProperty>>{{TEXT("attributes"),MP_MaterialAttributes},{TEXT("base_color"),MP_BaseColor},{TEXT("roughness"),MP_Roughness},{TEXT("normal"),MP_Normal},{TEXT("opacity_mask"),MP_OpacityMask}})
      Roots->SetObjectField(Entry.Key,Pin(M->GetExpressionInputForProperty(Entry.Value)));
    Row->SetObjectField(TEXT("roots"),Roots);Materials.Add(MakeShared<FJsonValueObject>(Row));
    for(UMaterialExpression* Expression:M->GetExpressions())if(Expression)Queue.Add(Expression);
  }
  for(int32 I=0;I<Queue.Num();++I){
    auto* E=Queue[I];if(Seen.Contains(E))continue;Seen.Add(E);
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("path"),E->GetPathName());Row->SetStringField(TEXT("type"),E->GetClass()->GetName());TArray<TSharedPtr<FJsonValue>> Inputs,Outputs;
    for(FExpressionInputIterator It{E};It;++It){
      auto Edge=Pin(It.Input);auto* Call=Cast<UMaterialExpressionMaterialFunctionCall>(E);
      Edge->SetStringField(TEXT("input"),(Call?Call->GetInputNameWithType(It.Index,false):E->GetInputName(It.Index)).ToString());Edge->SetNumberField(TEXT("input_index"),It.Index);
      Inputs.Add(MakeShared<FJsonValueObject>(Edge));if(It->Expression)Queue.Add(It->Expression);
    }
    for(int32 J=0;J<E->Outputs.Num();++J){auto V=MakeShared<FJsonObject>();V->SetNumberField(TEXT("index"),J);V->SetStringField(TEXT("name"),E->Outputs[J].OutputName.ToString());Outputs.Add(MakeShared<FJsonValueObject>(V));}
    Row->SetArrayField(TEXT("inputs"),Inputs);Row->SetArrayField(TEXT("outputs"),Outputs);
    if(auto* C=Cast<UMaterialExpressionCollectionParameter>(E)){Row->SetStringField(TEXT("collection"),GetPathNameSafe(C->Collection));Row->SetStringField(TEXT("parameter"),C->ParameterName.ToString());}
    if(auto* C=Cast<UMaterialExpressionMaterialFunctionCall>(E)){Row->SetStringField(TEXT("function"),GetPathNameSafe(C->MaterialFunction));if(C->MaterialFunction)for(UMaterialExpression* N:C->MaterialFunction->GetExpressions())if(N)Queue.Add(N);}
    if(auto* Usage=Cast<UMaterialExpressionNamedRerouteUsage>(E)){Row->SetStringField(TEXT("declaration"),GetPathNameSafe(Usage->Declaration));if(Usage->Declaration)Queue.Add(Usage->Declaration);}
    Nodes.Add(MakeShared<FJsonValueObject>(Row));
  }
  auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("status"),TEXT("PASS native roots and exact pin/output index graph inventory"));Result->SetArrayField(TEXT("materials"),Materials);Result->SetArrayField(TEXT("nodes"),Nodes);Result->SetBoolField(TEXT("asset_saves"),false);Result->SetBoolField(TEXT("world_loaded"),false);
  FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);if(FJsonSerializer::Serialize(Result,Writer)&&FFileHelper::SaveStringToFile(Text,*Args[0],FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {UE_LOG(LogCarla,Display,TEXT("WEATHER_GRAPH393_PASS nodes=%d materials=%d"),Nodes.Num(),Materials.Num());}
  else {UE_LOG(LogCarla,Error,TEXT("WEATHER_GRAPH393_FAILED write"));}
}
static FAutoConsoleCommand Command(TEXT("carla.Materials.AuditWeatherGraph"),TEXT("Read-only exact-pin audit in the owned native diagnostic project."),FConsoleCommandWithArgsDelegate::CreateStatic(&Run));
}
#endif
