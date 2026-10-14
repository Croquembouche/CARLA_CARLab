// Exercise the production gate without a GPU, using deterministic interleavings
// of the single producer and consumer. Live payload tests cover actual threads.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaReadbackPollGateTest,"Carla.Sensors.ReadbackPollGate",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaReadbackPollGateTest::RunTest(const FString&)
{
  FCarlaReadbackPollGate Gate;
  TestTrue(TEXT("First poll queues"),Gate.TryQueue(true));
  for(int32 I=0;I<1000;++I)
    TestFalse(TEXT("Busy render thread cannot accumulate polls"),Gate.TryQueue(true));
  TestEqual(TEXT("Only one poll outstanding"),Gate.NumOutstanding(),1);
  Gate.Complete();
  TestTrue(TEXT("Incomplete or newly arriving readback can be polled again"),Gate.TryQueue(true));
  Gate.Complete();
  TestTrue(TEXT("Control can queue first poll"),Gate.TryQueue(false));
  TestTrue(TEXT("Control can queue second poll"),Gate.TryQueue(false));
  TestFalse(TEXT("Enabling coalescing respects existing control backlog"),Gate.TryQueue(true));
  Gate.Complete();
  TestFalse(TEXT("Partial drain still blocks a redundant poll"),Gate.TryQueue(true));
  Gate.Complete();
  TestTrue(TEXT("Full drain resumes producer"),Gate.TryQueue(true));
  Gate.Complete();
  TestEqual(TEXT("All commands retired before shutdown"),Gate.NumOutstanding(),0);
  return true;
}
static FAutoConsoleCommand RunReadbackPollGateTest(TEXT("carla.Sensors.TestReadbackPollGate"),
    TEXT("Run the native production readback poll gate regression test."),
    FConsoleCommandDelegate::CreateLambda([] {
      auto& Framework=FAutomationTestFramework::Get();
      if(Framework.GetCurrentTest()){UE_LOG(LogCarla,Warning,TEXT("READBACK_POLL_TEST busy"));return;}
      Framework.StartTestByName(TEXT("FCarlaReadbackPollGateTest"),0,TEXT("Carla.Sensors.ReadbackPollGate"));
      if(!Framework.GetCurrentTest()){UE_LOG(LogCarla,Error,TEXT("READBACK_POLL_TEST result=Fail test_did_not_start"));return;}
      FAutomationTestExecutionInfo Info;const bool Passed=Framework.StopTest(Info);
      UE_LOG(LogCarla,Display,TEXT("READBACK_POLL_TEST result=%s"),Passed?TEXT("Success"):TEXT("Fail"));
    }));
