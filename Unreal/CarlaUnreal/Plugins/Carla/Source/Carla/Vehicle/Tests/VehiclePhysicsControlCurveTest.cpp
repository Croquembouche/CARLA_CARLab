#include "Carla/Vehicle/VehiclePhysicsControl.h"
#include "Carla.h"
#include <util/disable-ue4-macros.h>
#include <carla/rpc/VehiclePhysicsControl.h>
#include <util/enable-ue4-macros.h>
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehiclePhysicsControlCurveTest,
    "Carla.Vehicle.PhysicsCurveRoundTrip",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVehiclePhysicsControlCurveTest::RunTest(const FString& Parameters)
{
  for (bool Populated : {false,true}) {
    FVehiclePhysicsControl Native;
    if (Populated) {
      Native.TorqueCurve.AddKey(0.f,1.f);Native.TorqueCurve.AddKey(2200.f,.8f);
      Native.SteeringCurve.AddKey(0.f,1.f);Native.SteeringCurve.AddKey(20.f,.9f);
      Native.SteeringCurve.AddKey(60.f,.8f);Native.SteeringCurve.AddKey(120.f,.7f);
    }
    auto Reported=carla::rpc::VehiclePhysicsControl::FromFVehiclePhysicsControl(Native);
    TestEqual(TEXT("torque contains only authored keys"),int32(Reported.torque_curve.size()),Native.TorqueCurve.GetNumKeys());
    TestEqual(TEXT("steering contains only authored keys"),int32(Reported.steering_curve.size()),Native.SteeringCurve.GetNumKeys());
    auto Returned=static_cast<FVehiclePhysicsControl>(Reported);
    TestEqual(TEXT("torque keys survive round trip"),Returned.TorqueCurve.GetNumKeys(),Native.TorqueCurve.GetNumKeys());
    TestEqual(TEXT("steering keys survive round trip"),Returned.SteeringCurve.GetNumKeys(),Native.SteeringCurve.GetNumKeys());
    for (float Speed : {0.f,10.f,20.f,35.f,60.f,120.f,200.f}) {
      TestEqual(TEXT("steering interpolation unchanged"),Returned.SteeringCurve.Eval(Speed),Native.SteeringCurve.Eval(Speed));
      TestEqual(TEXT("torque interpolation unchanged"),Returned.TorqueCurve.Eval(Speed),Native.TorqueCurve.Eval(Speed));
    }
  }
  return true;
}
static FAutoConsoleCommand CCmdPhysicsCurveTest(
    TEXT("carla.Vehicle.TestPhysicsCurves"),TEXT("Verify exact physics curve RPC round trips."),
    FConsoleCommandDelegate::CreateLambda([]() {
      auto& Framework=FAutomationTestFramework::Get();
      if (Framework.GetCurrentTest()) { UE_LOG(LogCarla,Warning,TEXT("PHYSICS_CURVE_TEST busy")); return; }
      Framework.StartTestByName(TEXT("FVehiclePhysicsControlCurveTest"),0,TEXT("Carla.Vehicle.PhysicsCurveRoundTrip"));
      if (!Framework.GetCurrentTest()) { UE_LOG(LogCarla,Warning,TEXT("PHYSICS_CURVE_TEST result=Fail test_did_not_start")); return; }
      FAutomationTestExecutionInfo Info;
      bool Passed=Framework.StopTest(Info);
      UE_LOG(LogCarla,Display,TEXT("PHYSICS_CURVE_TEST result=%s"),Passed?TEXT("Success"):TEXT("Fail"));
    }));
#endif
