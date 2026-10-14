// Optional profiling queries. Payload readiness continues to use its readback fence.
namespace CameraCopyTiming
{
static TAutoConsoleVariable<int32> Enabled(TEXT("carla.Sensors.CameraCopyTiming"),1,
    TEXT("Emit the two optional camera-copy GPU profiling queries. Does not control payload readiness."),ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> Audit(TEXT("carla.Sensors.CameraCopyTimingAudit"),0,
    TEXT("Bounded actual-RHI copy timing audit, excluded from performance measurements."),ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> Verify(TEXT("carla.Sensors.CameraCopyTimingVerify"),0,
    TEXT("Diagnostic only: compare normal readback bytes with an independent timed readback of the same texture."),ECVF_RenderThreadSafe);
static TAtomic<uint32> Issued{0},Compared{0},Pending{0};
static uint32 NextAudit()
{
    if (!Audit.GetValueOnRenderThread()) return 0;
    const uint32 Serial=++Issued;
    return Serial<=64 ? Serial : 0;
}
static FAutoConsoleCommand Status(TEXT("carla.Sensors.CameraCopyTimingStatus"),TEXT("Report timing diagnostic state; reset only when audit and verification are disabled and drained."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        const bool Reset=Args.Num()==1 && Args[0]==TEXT("reset");
        ENQUEUE_RENDER_COMMAND(CarlaCameraCopyTimingStatus)([Reset](FRHICommandListImmediate&)
        {
            if (Reset && !Audit.GetValueOnRenderThread() && !Verify.GetValueOnRenderThread() && Pending.Load()==0) {Issued.Store(0);Compared.Store(0);}
            UE_LOG(LogCarla,Log,TEXT("CARLA_CAMERA_COPY_TIMING_STATUS timing=%d audit=%d verify=%d issued=%u compared=%u pending=%u actual_rt=%d"),
                Enabled.GetValueOnRenderThread(),Audit.GetValueOnRenderThread(),Verify.GetValueOnRenderThread(),FMath::Min(Issued.Load(),64u),Compared.Load(),Pending.Load(),IsInRenderingThread()?1:0);
        });
    }));
}
