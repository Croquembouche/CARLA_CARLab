// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include <Carla/Sensor/ImageUtil.h>
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <Carla/Sensor/ShaderBasedSensor.h>
#include <Carla/Carla.h>

#include <util/ue-header-guard-begin.h>
#include <RHISurfaceDataConversion.h>
#include <ImageWriteQueue.h>
#include <HighResScreenshot.h>
#include <RHIGPUReadback.h>
#include <util/ue-header-guard-end.h>

#include <chrono>
#include <thread>

template <typename F>
class ScopedCallback
{
    F fn;
public:

    constexpr ScopedCallback(F&& fn) : fn(fn) { }

    ~ScopedCallback() { fn(); }
};



namespace ImageUtil
{
  static TAutoConsoleVariable<int32> EarlyCameraSubmit(TEXT("carla.Sensors.EarlyCameraSubmit"),0,
      TEXT("Submit camera copy commands without blocking, before preparing later views."));
  #include "CarlaCameraCopyTiming.inl"
  bool DecodePixelsByFormat(
    const void* PixelData,
    int32 SourcePitch,
    FIntPoint Extent,
    EPixelFormat Format,
    FReadSurfaceDataFlags Flags,
    TArrayView<FLinearColor> Out)
  {
    SourcePitch *= GPixelFormats[Format].BlockBytes;
    return ConvertRAWSurfaceDataToFLinearColor(
      Format,
      Extent.X,
      Extent.Y,
      (uint8*)PixelData,
      SourcePitch,
      Out.GetData(),
      Flags);
  }



  bool DecodePixelsByFormat(
    const void* PixelData,
    int32 SourcePitch,
    FIntPoint Extent,
    EPixelFormat Format,
    FReadSurfaceDataFlags Flags,
    TArrayView<FColor> Out)
  {
    SourcePitch *= GPixelFormats[Format].BlockBytes;
    auto OutPixelCount = Extent.X * Extent.Y;
    switch (Format)
    {
    case PF_G16:
    case PF_R16_UINT:
    case PF_R16_SINT:
      // Shadow maps
      ConvertRawR16DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData());
      break;
    case PF_R8G8B8A8:
      ConvertRawR8G8B8A8DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData());
      break;
    case PF_B8G8R8A8:
      ConvertRawB8G8R8A8DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData());
      break;
    case PF_A2B10G10R10:
      ConvertRawR10G10B10A2DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData());
      break;
    case PF_FloatRGBA:
    case PF_R16G16B16A16_UNORM:
    case PF_R16G16B16A16_SNORM:
      ConvertRawR16G16B16A16FDataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData(), Flags.GetLinearToGamma());
      break;
    case PF_FloatR11G11B10:
      ConvertRawR11G11B10DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData(), Flags.GetLinearToGamma());
      break;
    case PF_A32B32G32R32F:
      ConvertRawR32G32B32A32DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData(), Flags.GetLinearToGamma());
      break;
    case PF_A16B16G16R16:
      ConvertRawR16G16B16A16DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData());
      break;
    case PF_G16R16:
      ConvertRawR16G16DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData());
      break;
    case PF_DepthStencil: // Depth / Stencil
      ConvertRawD32S8DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData(), Flags);
      break;
    case PF_X24_G8: // Depth / Stencil
      ConvertRawR24G8DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData(), Flags);
      break;
    case PF_R32_FLOAT: // Depth
		  for (uint32 Y = 0; Y < (uint32)Extent.Y; Y++)
		  {
		  	auto SrcPtr = (float*)((uint8*)PixelData + Y * SourcePitch);
		  	auto DestPtr = Out.GetData() + Y * Extent.X;
		  	for (uint32 X = 0; X < (uint32)Extent.X; X++)
		  	{
		  		*DestPtr = FLinearColor(SrcPtr[0], 0.f, 0.f, 1.f).QuantizeRound();
		  		++SrcPtr;
		  		++DestPtr;
		  	}
		  }
      break;
    case PF_R16G16B16A16_UINT:
    case PF_R16G16B16A16_SINT:
      ConvertRawR16G16B16A16DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData());
      break;
    case PF_G8:
      ConvertRawR8DataToFColor(Extent.X, Extent.Y, (uint8*)PixelData, SourcePitch, Out.GetData());
      break;
    default:
      UE_LOG(LogCarla, Warning, TEXT("Unsupported format %llu"), (unsigned long long)Format);
      return false;
    }
    return true;
  }



  bool ReadImageData(
    UTextureRenderTarget2D& RenderTarget,
    TArray<FColor>& Out)
  {
    check(IsInGameThread());
    auto Resource = RenderTarget.GameThread_GetRenderTargetResource();
    FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
    ReadFlags.SetLinearToGamma(true);
    return Resource->ReadPixels(Out, ReadFlags);
  }



  bool ReadImageData(
    UTextureRenderTarget2D& RenderTarget,
    TArray64<FColor>& Out)
  {
    auto Resource = RenderTarget.GameThread_GetRenderTargetResource();
    FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
    ReadFlags.SetLinearToGamma(true);
    Out.SetNum(RenderTarget.GetSurfaceWidth() * RenderTarget.GetSurfaceHeight());
    return Resource->ReadPixelsPtr(Out.GetData(), ReadFlags);
  }



  TUniquePtr<TImagePixelData<FColor>> ReadImageData(
    UTextureRenderTarget2D& RenderTarget)
  {
    const auto Size = FIntPoint(
      RenderTarget.GetSurfaceWidth(),
      RenderTarget.GetSurfaceHeight());
    auto PixelData = MakeUnique<TImagePixelData<FColor>>(Size);
    ReadImageData(RenderTarget, PixelData->Pixels);
    return PixelData;
  }



  TFuture<bool> SaveImageData(
    UTextureRenderTarget2D& RenderTarget,
    const FStringView& Path)
  {
    return SaveImageData(ReadImageData(RenderTarget), Path);
  }



  TFuture<bool> SaveImageData(
    TUniquePtr<TImagePixelData<FColor>> Data,
    const FStringView& Path)
  {
    auto& HighResScreenshotConfig = GetHighResScreenshotConfig();
    auto ImageTask = MakeUnique<FImageWriteTask>();
    ImageTask->PixelData = MoveTemp(Data);
    ImageTask->Filename = Path;
    ImageTask->Format = EImageFormat::PNG;
    ImageTask->CompressionQuality = (int32)EImageCompressionQuality::Default;
    ImageTask->bOverwriteFile = true;
    ImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));
    return HighResScreenshotConfig.ImageWriteQueue->Enqueue(MoveTemp(ImageTask));
  }



  struct ReadImageDataContext
  {
    EPixelFormat Format;
    FIntPoint Size;
    ReadImageDataAsyncCallback Callback;
    // Pool-owned readback: lifetime managed by Pool/SlotIndex pair. Pointer
    // is non-owning when Pool is set, owning (via FallbackReadback) otherwise.
    FRHIGPUTextureReadback* Readback = nullptr;
    TUniquePtr<FRHIGPUTextureReadback> FallbackReadback;
    FRHIGPUReadbackPoolPtr Pool;
    int32 SlotIndex = INDEX_NONE;
    double Enqueued=0,Requested=0;
    FRenderQueryRHIRef CopyStart,CopyEnd,ReferenceStart,ReferenceEnd;
    TUniquePtr<FRHIGPUTextureReadback> ReferenceReadback;
    uint32 TimingAuditSerial=0;
    bool CopyTimingEnabled=true;
  };

  static void ReadImageDataBegin(
    ReadImageDataContext& Self,
    UTextureRenderTarget2D& RenderTarget,
    FRHIGPUReadbackPoolPtr Pool,
    ReadImageDataAsyncCallback&& Callback)
  {
    auto& CmdList = FRHICommandListImmediate::Get();
    auto Resource = static_cast<FTextureRenderTarget2DResource*>(
      RenderTarget.GetResource());
    if (Resource == nullptr)
      return;
    auto Texture = Resource->GetRenderTargetTexture();
    if (Texture == nullptr)
      return;
    Self.Callback = std::move(Callback);
    Self.Pool = std::move(Pool);
    if (Self.Pool)
    {
      Self.Readback = Self.Pool->Acquire(Self.SlotIndex);
    }
    if (Self.Readback == nullptr)
    {
      Self.FallbackReadback = MakeUnique<FRHIGPUTextureReadback>(
        TEXT("ReadImageData-Readback"));
      Self.Readback = Self.FallbackReadback.Get();
    }
    Self.Size = Texture->GetSizeXY();
    Self.Format = Texture->GetFormat();
    auto ResolveRect = FResolveRect();
    CmdList.Transition(FRHITransitionInfo(Texture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
    Self.Enqueued=FPlatformTime::Seconds();
    if(Self.Requested>0)CarlaGpuSensors::ProfileSample(TEXT("camera_readback_request_to_enqueue_ms"),(Self.Enqueued-Self.Requested)*1000);
    Self.CopyTimingEnabled=CameraCopyTiming::Enabled.GetValueOnRenderThread()!=0;
    Self.TimingAuditSerial=CameraCopyTiming::NextAudit();
    if (Self.CopyTimingEnabled && GSupportsTimestampRenderQueries) {Self.CopyStart=RHICreateRenderQuery(RQT_AbsoluteTime);Self.CopyEnd=RHICreateRenderQuery(RQT_AbsoluteTime);CmdList.EndRenderQuery(Self.CopyStart);}
    Self.Readback->EnqueueCopy(CmdList, Texture, ResolveRect);
    if (Self.CopyEnd) CmdList.EndRenderQuery(Self.CopyEnd);
    if (CameraCopyTiming::Verify.GetValueOnRenderThread()!=0)
    {
      Self.ReferenceReadback=MakeUnique<FRHIGPUTextureReadback>(TEXT("CameraCopyTiming-Reference"));
      if (GSupportsTimestampRenderQueries)
      {
        Self.ReferenceStart=RHICreateRenderQuery(RQT_AbsoluteTime);Self.ReferenceEnd=RHICreateRenderQuery(RQT_AbsoluteTime);
        CmdList.EndRenderQuery(Self.ReferenceStart);
      }
      Self.ReferenceReadback->EnqueueCopy(CmdList,Texture,ResolveRect);
      if (Self.ReferenceEnd) CmdList.EndRenderQuery(Self.ReferenceEnd);
      ++CameraCopyTiming::Pending;
    }
    if (Self.TimingAuditSerial)
    {
      const uint32 Serial=Self.TimingAuditSerial;
      const bool Timing=Self.CopyTimingEnabled,Supported=GSupportsTimestampRenderQueries;
      const int32 Writes=(Self.CopyStart?1:0)+(Self.CopyEnd?1:0),ReferenceWrites=(Self.ReferenceStart?1:0)+(Self.ReferenceEnd?1:0);
      const FIntPoint Size=Self.Size;
      CmdList.EnqueueLambda([Serial,Timing,Supported,Writes,ReferenceWrites,Size](FRHICommandListImmediate&)
      {
        UE_LOG(LogCarla,Log,TEXT("CARLA_CAMERA_COPY_TIMING serial=%u timing=%d supported=%d query_writes=%d reference_query_writes=%d width=%d height=%d actual_rhi=%d"),
          Serial,Timing?1:0,Supported?1:0,Writes,ReferenceWrites,Size.X,Size.Y,IsInRHIThread()?1:0);
      });
    }
    CmdList.Transition(FRHITransitionInfo(Texture, ERHIAccess::CopySrc, ERHIAccess::SRVMask));

    // Submit this capture/copy while the CPU prepares later cameras and the
    // maintenance view. Keep completion asynchronous: this does not wait for
    // the RHI thread or GPU, and the existing pool/fence owns the readback.
    if(EarlyCameraSubmit.GetValueOnRenderThread()!=0) {
      TRACE_CPUPROFILER_EVENT_SCOPE(CarlaCameraEarlySubmit);
      const double Started=FPlatformTime::Seconds();
      CmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
      CarlaGpuSensors::ProfileSample(TEXT("camera_submit_ms"),(FPlatformTime::Seconds()-Started)*1000);
    }

  }

  static void ReadImageDataRelease(ReadImageDataContext& Self)
  {
    if (Self.Pool && Self.SlotIndex != INDEX_NONE)
    {
      Self.Pool->Release(Self.SlotIndex);
      Self.SlotIndex = INDEX_NONE;
    }
  }

  static void ReadImageDataEnd(ReadImageDataContext&& Self)
  {
    check(IsInRenderingThread());
    int32 RowPitch, BufferHeight;
    auto MappedPtr = Self.Readback->Lock(RowPitch, &BufferHeight);

    if (!MappedPtr) { if(Self.ReferenceReadback) --CameraCopyTiming::Pending; ReadImageDataRelease(Self); return; }
    if (Self.ReferenceReadback)
    {
      int32 ReferencePitch=0,ReferenceHeight=0;
      const void* Reference=Self.ReferenceReadback->Lock(ReferencePitch,&ReferenceHeight);
      const auto& Info=GPixelFormats[Self.Format];
      bool Match=Reference && Info.BlockSizeX==1 && Info.BlockSizeY==1 && RowPitch>=Self.Size.X && ReferencePitch>=Self.Size.X && BufferHeight>=Self.Size.Y && ReferenceHeight>=Self.Size.Y;
      uint64 ComparedBytes=0;
      if (Match)
      {
        for (int32 Row=0;Row<Self.Size.Y;++Row)
        {
          const SIZE_T Bytes=SIZE_T(Self.Size.X)*Info.BlockBytes;
          const uint8* ActualRow=static_cast<const uint8*>(MappedPtr)+SIZE_T(Row)*RowPitch*Info.BlockBytes;
          const uint8* ReferenceRow=static_cast<const uint8*>(Reference)+SIZE_T(Row)*ReferencePitch*Info.BlockBytes;
          Match=(FMemory::Memcmp(ActualRow,ReferenceRow,Bytes)==0) && Match;ComparedBytes+=Bytes;
        }
      }
      if(Reference) Self.ReferenceReadback->Unlock();
      Self.ReferenceReadback.Reset();--CameraCopyTiming::Pending;++CameraCopyTiming::Compared;
      UE_LOG(LogCarla,Log,TEXT("CARLA_CAMERA_COPY_TIMING_COMPARE serial=%u timing=%d width=%d height=%d bytes=%llu matched=%d actual_rt=%d"),
        Self.TimingAuditSerial,Self.CopyTimingEnabled?1:0,Self.Size.X,Self.Size.Y,ComparedBytes,Match?1:0,IsInRenderingThread()?1:0);
    }
    // RHI mapping/unmapping stays on the render thread. Keep the staging slot
    // locked while a background task converts its pixels, without a raw copy.
    CarlaGpuSensors::DispatchReadbackCallback([
      Self = std::move(Self), MappedPtr, RowPitch, BufferHeight]() mutable
    {
      ScopedCallback Release = [&Self]
      {
        ENQUEUE_RENDER_COMMAND(CarlaUnmapImageReadback)([
          Self = std::move(Self)](FRHICommandListImmediate&) mutable
        {
          Self.Readback->Unlock();
          ReadImageDataRelease(Self);
        });
      };
      const double Started=FPlatformTime::Seconds();
      Self.Callback(MappedPtr, RowPitch, BufferHeight, Self.Format, Self.Size);
      CarlaGpuSensors::ProfileSample(TEXT("camera_convert_serialize_ms"),(FPlatformTime::Seconds()-Started)*1000);
    });
  }

  static void ReadImageDataEndAsync(
    ReadImageDataContext&& Self)
  {
    if (!Self.Readback) return;
    CarlaGpuSensors::PollReadback([Self = std::move(Self)]() mutable
    {
      if (!Self.Readback->IsReady() || (Self.ReferenceReadback && !Self.ReferenceReadback->IsReady())) return false;
      CarlaGpuSensors::ProfileSample(TEXT("camera_readback_ready_ms"),(FPlatformTime::Seconds()-Self.Enqueued)*1000);
      uint64 Start=0,End=0;
      if (Self.CopyStart && RHIGetRenderQueryResult(Self.CopyStart,Start,false) && RHIGetRenderQueryResult(Self.CopyEnd,End,false) && End>=Start)
        CarlaGpuSensors::ProfileSample(TEXT("camera_copy_gpu_ms"),double(End-Start)/1000.);
      ReadImageDataEnd(std::move(Self));
      return true;
    });
  }

  static void ReadImageDataAsyncCommand(
    double Requested,
    UTextureRenderTarget2D& RenderTarget,
    FRHIGPUReadbackPoolPtr Pool,
    ReadImageDataAsyncCallback&& Callback)
  {
    ReadImageDataContext Context = { };
    Context.Requested=Requested;
    ReadImageDataBegin(Context, RenderTarget, std::move(Pool), std::move(Callback));
    ReadImageDataEndAsync(std::move(Context));
  }



  bool ReadImageDataAsync(
    UTextureRenderTarget2D& RenderTarget,
    FRHIGPUReadbackPoolPtr Pool,
    ReadImageDataAsyncCallback&& Callback)
  {
    const double Requested=FPlatformTime::Seconds();
    if (IsInRenderingThread())
    {
      ReadImageDataAsyncCommand(
        Requested,
        RenderTarget,
        std::move(Pool),
        std::move(Callback));
    }
    else
    {
      ENQUEUE_RENDER_COMMAND(ReadImageDataAsyncCmd)([
        Requested, &RenderTarget, Pool = std::move(Pool), Callback = std::move(Callback)
      ](auto& CmdList) mutable
      {
        ReadImageDataContext Context = { };
        Context.Requested=Requested;
        ReadImageDataBegin(Context, RenderTarget, std::move(Pool), std::move(Callback));
        ReadImageDataEndAsync(std::move(Context));
      });

    }
    return true;
  }



  bool ReadImageDataAsync(
    UTextureRenderTarget2D& RenderTarget,
    ReadImageDataAsyncCallback&& Callback)
  {
    return ReadImageDataAsync(RenderTarget, nullptr, std::move(Callback));
  }



  bool ReadSensorImageDataAsync(
    AShaderBasedSensor& Sensor,
    ReadImageDataAsyncCallback&& Callback)
  {
    auto RenderTarget = Sensor.GetCaptureRenderTarget();
    if (RenderTarget == nullptr)
      return false;
    return ReadImageDataAsync(
      *RenderTarget,
      Sensor.GetReadbackPool(),
      std::move(Callback));
  }



  bool ReadImageDataAsyncFColor(
    UTextureRenderTarget2D& RenderTarget,
    ReadImageDataAsyncCallbackFColor&& Callback)
  {
    return ReadImageDataAsync(RenderTarget, [Callback = std::move(Callback)](
      const void* Mapping,
      size_t RowPitch,
      size_t BufferHeight,
      EPixelFormat Format,
      FIntPoint Size) -> bool
    {
      FReadSurfaceDataFlags Flags;
      TArray<FColor> Pixels;
      Pixels.SetNum(Size.X * Size.Y);
      if (!DecodePixelsByFormat(Mapping, RowPitch, Size, Format, Flags, Pixels))
        return false;
      return Callback(Pixels, Size);
    });
  }



  bool ReadSensorImageDataAsyncFColor(
    AShaderBasedSensor& Sensor,
    ReadImageDataAsyncCallbackFColor&& Callback)
  {
    auto RenderTarget = Sensor.GetCaptureRenderTarget();
    if (RenderTarget == nullptr)
      return false;
    TWeakObjectPtr<AShaderBasedSensor> WeakSensor(&Sensor);
    return ReadImageDataAsync(*RenderTarget, Sensor.GetReadbackPool(),
      [WeakSensor, Callback = std::move(Callback)](
        const void* Mapping, size_t RowPitch, size_t BufferHeight,
        EPixelFormat Format, FIntPoint Size) mutable -> bool
      {
        FReadSurfaceDataFlags Flags;
        TArray<FColor> Pixels;
        Pixels.SetNum(Size.X * Size.Y);
        if (!DecodePixelsByFormat(Mapping, RowPitch, Size, Format, Flags, Pixels)) return false;
        CarlaGpuSensors::EnqueueGameThread([
          WeakSensor, Callback = std::move(Callback), Pixels = MoveTemp(Pixels), Size]() mutable
        {
          if (WeakSensor.IsValid() && WeakSensor->HasActorBegunPlay()) Callback(Pixels, Size);
        });
        return true;
      });
  }



  bool ReadImageDataAsyncFLinearColor(
    UTextureRenderTarget2D& RenderTarget,
    ReadImageDataAsyncCallbackFLinearColor&& Callback)
  {
    return ReadImageDataAsync(RenderTarget, [Callback = std::move(Callback)](
      const void* Mapping,
      size_t RowPitch,
      size_t BufferHeight,
      EPixelFormat Format,
      FIntPoint Size) -> bool
      {
        FReadSurfaceDataFlags Flags;
        TArray<FLinearColor> Pixels;
        Pixels.SetNum(Size.X * Size.Y);
        if (!DecodePixelsByFormat(Mapping, RowPitch, Size, Format, Flags, Pixels))
          return false;
        return Callback(Pixels, Size);
      });
  }



  bool ReadSensorImageDataAsyncFLinearColor(
    AShaderBasedSensor& Sensor,
    ReadImageDataAsyncCallbackFLinearColor&& Callback)
  {
    auto RenderTarget = Sensor.GetCaptureRenderTarget();
    if (RenderTarget == nullptr)
      return false;
    TWeakObjectPtr<AShaderBasedSensor> WeakSensor(&Sensor);
    return ReadImageDataAsync(*RenderTarget, Sensor.GetReadbackPool(),
      [WeakSensor, Callback = std::move(Callback)](
        const void* Mapping, size_t RowPitch, size_t BufferHeight,
        EPixelFormat Format, FIntPoint Size) mutable -> bool
      {
        FReadSurfaceDataFlags Flags;
        TArray<FLinearColor> Pixels;
        Pixels.SetNum(Size.X * Size.Y);
        if (!DecodePixelsByFormat(Mapping, RowPitch, Size, Format, Flags, Pixels)) return false;
        CarlaGpuSensors::EnqueueGameThread([
          WeakSensor, Callback = std::move(Callback), Pixels = MoveTemp(Pixels), Size]() mutable
        {
          if (WeakSensor.IsValid() && WeakSensor->HasActorBegunPlay()) Callback(Pixels, Size);
        });
        return true;
      });
  }
}
