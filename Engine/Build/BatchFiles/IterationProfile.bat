@call %~dp0RunUAT.bat BuildGraph -Script=Engine/Build/IterationProfile.xml -Target="Run Iteration Profile Tests" -append:ProjectNames="ShooterGame" -set:Win64=true %*
