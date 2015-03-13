#include <unistd.h>
#include <thread>

#include "WPILib.h"
#include "Robot.h"
#include "config.h"

void Apollo::RobotInit() {
	drive = new RobotDrive(FRONT_LEFT_MOTOR_PWM, REAR_LEFT_MOTOR_PWM,
						   FRONT_RIGHT_MOTOR_PWM, REAR_RIGHT_MOTOR_PWM);
	joystick = new Joystick(JOY_PORT_0);
	liftTalon = new Talon(LIFT_PWM);
	shifter = new DoubleSolenoid(SHIFT_UP, SHIFT_DOWN);
	claw = new DoubleSolenoid(OPEN_CLAW, CLOSE_CLAW);
	compressor = new Compressor(PCM_NODE_ID);
	gyro = new Gyro(GYRO_PWM);
	gyro->Reset();
	stringPot = new AnalogInput(STRING_POT_PWM);
	topLimitSwitch = new DigitalInput(TOP_LIMIT_SWITCH);
	botLimitSwitch = new DigitalInput(BOT_LIMIT_SWITCH);
	LED = new DigitalOutput(LED_PIN);

	drive->SetSafetyEnabled(false);

	CameraServer::GetInstance()->SetQuality(50);
	//the camera name (ex "cam0") can be found through the roborio web interface
	CameraServer::GetInstance()->StartAutomaticCapture("cam0");

	driveThread = new std::thread(driveFunc);
	driveThread->detach();
	inputThread = new std::thread(inputFunc);
	inputThread->detach();
}

void Apollo::AutonomousInit() {
	switch(std::stoi(SmartDashboard::GetString("DB/String 9"))) {
	case 2: //The Luigi strat
		compressor->Start();
		break;
	case 1: //The Gratuitously Simple strat
		compressor->Start();

		//Drive forwards
		drive->MecanumDrive_Cartesian(0.f, -0.35f, 0.f);
		Wait(3.9f);
		drive->MecanumDrive_Cartesian(0.f, 0.35f, 0.f);
		Wait(0.2f);
		drive->MecanumDrive_Cartesian(0.f, 0.f, 0.f);
		break;
	case 0: //The OG strat
	default:
		compressor->Start();

		//Lift bin
		claw->Set(DoubleSolenoid::kForward);
		liftTalon->Set(0.75f);
		Wait(1.f);
		shifter->Set(DoubleSolenoid::kReverse);
		Wait(2.f);
		liftTalon->Set(0.f);

		bool *quit = new bool(false);
		std::thread *strafeThread = new std::thread([quit] () -> void {
			gyro->Reset();
			while(!(*quit)) {
				drive->MecanumDrive_Cartesian(0.5f, 0.f, -gyro->GetAngle() / 1800.f);
			}
		});
		strafeThread->detach();
		Wait(1.f);
		*quit = true;
		Wait(0.1f);
		delete quit;
		delete strafeThread;

		//Drive forward
		drive->MecanumDrive_Cartesian(0.f, -0.35f, 0.f);
		Wait(4.f);
		drive->MecanumDrive_Cartesian(0.f, 0.35f, 0.f);
		Wait(0.2f);
		drive->MecanumDrive_Cartesian(0.f, 0.f, 0.f);
		break;
	}
}

void Apollo::TeleopInit() {
	driveRun = true;
	compressor->Start();
	gyro->Reset();
	gyroAngle = gyro->GetAngle();
}

void Apollo::DisabledInit() {
	driveRun = false;
	compressor->Stop();
}

void driveFunc() {
	float Kp = 0.024000; //A
	float Ki = 0.021000; //O
	float Kd = 0;        //L

	//mail

	SmartDashboard::PutString("DB/String 0", std::to_string(Kp));
	SmartDashboard::PutString("DB/String 1", std::to_string(Ki));
	SmartDashboard::PutString("DB/String 2", std::to_string(Kd));

	float xPIDError = 0;
	float xPIDIntegral = 0;
	float xCurrentSpeed = 0;
	float yPIDError = 0;
	float yPIDIntegral = 0;
	float yCurrentSpeed = 0;
	float zPIDError = 0;
	float zPIDIntegral = 0;
	float zCurrentSpeed = 0;

	bool precisionMode = true;
	float precisionFactor = 0.5f;

	double oldtime = GetTime();
	while (true) {
		SmartDashboard::PutString("DB/String 8", std::to_string(stringPot->GetVoltage()));

		double ctime = GetTime();
		if (driveRun) {
			SmartDashboard::PutString("DB/String 3", std::to_string(gyro->GetAngle()));
			SmartDashboard::PutString("DB/String 4", std::to_string(gyroAngle - gyro->GetAngle()));

			Kp = std::stof(SmartDashboard::GetString("DB/String 0"));
			Ki = std::stof(SmartDashboard::GetString("DB/String 1"));
			Kd = std::stof(SmartDashboard::GetString("DB/String 2"));
			//saving K values in dashboard q:^ )

			precisionMode = !joystick->GetRawButton(JOY_BTN_RBM);

			//X PID loop
			float xCurrentError = joystick->GetRawAxis(JOY_AXIS_LX) - xCurrentSpeed;
			xPIDIntegral += xPIDError * (ctime - oldtime);
			float xPIDderivative = (xCurrentError - xPIDError) / (ctime - oldtime);
			xCurrentSpeed += (Kp * xCurrentError) + (Ki * xPIDIntegral) + (Kd * xPIDderivative);
			xPIDError = xCurrentError;

			//Y PID loop
			float yCurrentError = joystick->GetRawAxis(JOY_AXIS_LY) - yCurrentSpeed;
			yPIDIntegral += yPIDError * (ctime - oldtime);
			float yPIDderivative = (yCurrentError - yPIDError) / (ctime - oldtime);
			yCurrentSpeed += (Kp * yCurrentError) + (Ki * yPIDIntegral) + (Kd * yPIDderivative);
			yPIDError = yCurrentError;

			//Rotate PID loop
			float zCurrentError = joystick->GetRawAxis(JOY_AXIS_RX) - zCurrentSpeed;
			if(fabs(joystick->GetRawAxis(JOY_AXIS_LX)) < 0.08 && fabs(joystick->GetRawAxis(JOY_AXIS_RX)) < 0.01) {
				if(fabs(joystick->GetRawAxis(JOY_AXIS_LY) > 0.5))
					zCurrentSpeed += (gyroAngle - gyro->GetAngle()) / 1800.f;
				else
					gyroAngle = gyro->GetAngle();
			} else {
				gyroAngle = gyro->GetAngle();
			}
			zPIDIntegral += zPIDError * (ctime - oldtime);
			float zPIDderivative = (zCurrentError - zPIDError) / (ctime - oldtime);
			zCurrentSpeed += (Kp * zCurrentError) + (Ki * zPIDIntegral) + (Kd * zPIDderivative);
			zPIDError = zCurrentError;

			xCurrentSpeed = safe_motor(xCurrentSpeed);
			yCurrentSpeed = safe_motor(yCurrentSpeed);
			zCurrentSpeed = safe_motor(zCurrentSpeed);
			SmartDashboard::PutString("DB/String 5", std::to_string(xCurrentSpeed));
			SmartDashboard::PutString("DB/String 6", std::to_string(yCurrentSpeed));
			SmartDashboard::PutString("DB/String 7", std::to_string(zCurrentSpeed));

			if (precisionMode) {
				drive->MecanumDrive_Cartesian(std::min(1.0, 1.2 * xCurrentSpeed * precisionFactor),
											  0.9 * yCurrentSpeed * precisionFactor,
											  0.9 * zCurrentSpeed * precisionFactor);
			} else {
				drive->MecanumDrive_Cartesian(std::min(1.0, 1.2 * xCurrentSpeed),
											  0.9 * yCurrentSpeed,
											  0.9 * zCurrentSpeed);
			}
		}
		oldtime = ctime;
	}
}

void inputFunc() {
	while(true) {
		if(!driveRun) continue;
		if(joystick->GetRawButton(JOY_BTN_LBM) && !topLimitSwitch->Get()) {
			liftTalon->Set(0.75f);
			motorStatus = up;
		} else if(joystick->GetRawButton(JOY_BTN_LTG) && !botLimitSwitch->Get()) {
			liftTalon->Set(-0.75f);
			motorStatus = down;
		} else {
			liftTalon->Set(0.f);
			motorStatus = stay;
		}

		if(joystick->GetRawButton(JOY_BTN_B)) {
			shifter->Set(DoubleSolenoid::kForward);
		} else if(joystick->GetRawButton(JOY_BTN_A)) {
			shifter->Set(DoubleSolenoid::kReverse);
		}

		if(joystick->GetRawButton(JOY_BTN_X)) {
			claw->Set(DoubleSolenoid::kForward);
		} else if(joystick->GetRawButton(JOY_BTN_Y)) {
			claw->Set(DoubleSolenoid::kReverse);
		}

		if(joystick->GetRawAxis(JOY_AXIS_DY) > 0.8) {
			LED->Set(0);
		}
		if(joystick->GetRawAxis(JOY_AXIS_DY) < -0.8) {
			LED->Set(1);
		}
	}
}

START_ROBOT_CLASS(Apollo);
