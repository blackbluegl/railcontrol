#include <cstring>    //memset
#include <sstream>
#include <string>

#include "Network/Select.h"
#include "Hardware/M6051.h"
#include "Utils/Utils.h"

using std::string;

namespace Hardware
{

	// create instance of m6051
	extern "C" M6051* create_m6051(const HardwareParams* params)
	{
		return new M6051(params);
	}

	// delete instance of m6051
	extern "C" void destroy_m6051(M6051* m6051)
	{
		delete(m6051);
	}

	M6051::M6051(const HardwareParams* params)
	:	HardwareInterface(params->manager, params->controlID, "Maerklin Interface (6050/6051) / " + params->name + " at serial port " + params->arg1),
	 	logger(Logger::Logger::GetLogger("M6051 " + params->name + " " + params->arg1)),
	 	serialLine(logger, params->arg1, B2400, 8, 'N', 2),
		run(true)
	{
		logger->Info(name);

		s88Modules = Utils::Utils::StringToInteger(params->arg2, 0, MaxS88Modules);
		if (s88Modules == 0)
		{
			logger->Info("No S88 modules configured.");
			return;
		}
		logger->Info("{0} S88 modules configured.", s88Modules);
		s88Thread = std::thread(&Hardware::M6051::S88Worker, this);
	}

	M6051::~M6051()
	{
		run = false;
		if (s88Modules > 0)
		{
			s88Thread.join();
		}
	}

	void M6051::Booster(const boosterState_t status)
	{
		if (!serialLine.IsConnected())
		{
			return;
		}

		unsigned char c;

		if (status)
		{
			logger->Info("Turning booster on");
			c = 96;
		}
		else
		{
			logger->Info("Turning booster off");
			c = 97;
		}
		serialLine.Send(c);
	}

	void M6051::LocoSpeed(__attribute__((unused)) const protocol_t& protocol, const address_t& address, const locoSpeed_t& speed)
	{
		if (!serialLine.IsConnected())
		{
			return;
		}
		unsigned char speedMM = (speed / 69) + (GetSpeedMapEntry(address) & 16);
		speedMap[address] = speedMM;
		unsigned char addressMM = static_cast<unsigned char>(address);
		logger->Info("Setting speed of loco {0} to speed {1}", address, speedMM);
		SendTwoBytes(speedMM, addressMM);
	}

	void M6051::LocoDirection(__attribute__((unused)) const protocol_t& protocol, const address_t& address, __attribute__((unused)) const direction_t& direction)
	{
		if (!serialLine.IsConnected())
		{
			return;
		}
		logger->Info("Changing direction of loco {0}", address);
		unsigned char speedMM = 15 + (GetSpeedMapEntry(address) & 16);
		unsigned char addressMM = static_cast<unsigned char>(address);
		SendTwoBytes(speedMM, addressMM);
	}

	void M6051::LocoFunction(__attribute__((unused)) const protocol_t protocol, const address_t address, const function_t function, const bool on)
	{
		if (function > 4)
		{
			return;
		}

		if (!serialLine.IsConnected())
		{
			return;
		}

		logger->Info("Setting f{0} of loco {1} to \"{2}\"", function, address, on ? "on" : "off");
		unsigned char addressMM = static_cast<unsigned char>(address);
		if (function == 0)
		{
			unsigned char speedMM = (GetSpeedMapEntry(address) & 15) + (static_cast<unsigned char>(on) << 4);
			speedMap[address] = speedMM;
			SendTwoBytes(speedMM, addressMM);
			return;
		}

		unsigned char functionMM = GetFunctionMapEntry(address);
		unsigned char position = function - 1;
		functionMM &= (~(1 << position)); // mask out related function
		functionMM |= (static_cast<unsigned char>(on) << position); // add related function
		functionMap[address] = functionMM;
		functionMM += 64;
		SendTwoBytes(functionMM, addressMM);
	}

	void M6051::Accessory(__attribute__((unused)) const protocol_t protocol, const address_t address, const accessoryState_t state, const bool on)
	{
		if (!serialLine.IsConnected())
		{
			return;
		}

		std::string stateText;
		DataModel::Accessory::Status(state, stateText);
		logger->Info("Setting state of accessory {0}/{1} to \"{2}\"", address, stateText, on ? "on" : "off");
		const unsigned char stateMM = (state == DataModel::Accessory::AccessoryStateOn ? 33 : 34);
		const unsigned char addressMM = static_cast<unsigned char>(address);
		SendTwoBytes(stateMM, addressMM);
	}

	void M6051::S88Worker()
	{
		Utils::Utils::SetThreadName("M6051");
		const unsigned char s88DoubleModules = ((s88Modules + 1) / 2);
		const unsigned char command = 128 + s88DoubleModules;
		const unsigned char s88SingleModules = (s88DoubleModules * 2);
		while(run && serialLine.IsConnected())
		{
			serialLine.ClearBuffers();
			serialLine.Send(command);
			for (unsigned char module = 0; module < s88SingleModules; ++module)
			{
				string data;
				bool ret = serialLine.Receive(data, 1);
				if (ret <= 0)
				{
					logger->Error("Error while reading data from interface");
					break;
				}
				unsigned char byte = data[0];

				if (byte != s88Memory[module])
				{
					unsigned char xorByte = byte ^ s88Memory[module];
					for(unsigned char pin = 1; pin <= 8; ++pin)
					{
						unsigned char shift = 8 - pin;
						if (((xorByte >> shift) & 0x01) == 0)
						{
							continue;
						}

						std::string text;
						DataModel::Feedback::feedbackState_t state;
						if ((byte >> shift) & 0x01)
						{
							text = "on";
							state = DataModel::Feedback::FeedbackStateOccupied;
						}
						else
						{
							text = "off";
							state = DataModel::Feedback::FeedbackStateFree;
						}
						address_t address = (module * 8) + pin;
						logger->Info("S88 Pin {0} set to {1}", address, text);
						manager->FeedbackState(controlID, address, state);
					}
					s88Memory[module] = byte;
				}
			}
			std::this_thread::yield();
		}
	}
} // namespace