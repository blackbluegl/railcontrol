#pragma once

#include <string>

#include "HardwareInterface.h"
#include "HardwareParams.h"
#include "Logger/Logger.h"
#include "Network/Serial.h"

namespace Hardware
{
	class M6051 : HardwareInterface
	{
		public:
			M6051(const HardwareParams* params);
			~M6051();

			bool CanHandleLocos() const override { return true; }
			bool CanHandleAccessories() const override { return true; }
			bool CanHandleFeedback() const override { return true; }

			void GetLocoProtocols(std::vector<protocol_t>& protocols) const override { protocols.push_back(ProtocolMM2); }

			bool LocoProtocolSupported(protocol_t protocol) const override { return (protocol == ProtocolMM2); }

			void GetAccessoryProtocols(std::vector<protocol_t>& protocols) const override { protocols.push_back(ProtocolMM2); }

			bool AccessoryProtocolSupported(protocol_t protocol) const override { return (protocol == ProtocolMM2); }

			void GetArgumentTypes(std::map<unsigned char,argumentType_t>& argumentTypes) const override
			{
				argumentTypes[1] = SerialPort;
				argumentTypes[2] = S88Modules;
			}

			void Booster(const boosterState_t status) override;
			void LocoSpeed(const protocol_t& protocol, const address_t& address, const locoSpeed_t& speed) override;
			void LocoDirection(const protocol_t& protocol, const address_t& address, const direction_t& direction) override;
			void LocoFunction(const protocol_t protocol, const address_t address, const function_t function, const bool on) override;
			void Accessory(const protocol_t protocol, const address_t address, const accessoryState_t state, const bool on) override;

		private:
			Logger::Logger* logger;
			static const unsigned char MaxS88Modules = 62;
			Network::Serial serialLine;
			volatile bool run;
			unsigned char s88Modules;
			std::thread s88Thread;
			unsigned char s88Memory[MaxS88Modules];
			std::map<address_t, unsigned char> speedMap;
			std::map<address_t, unsigned char> functionMap;

			unsigned char GetSpeedMapEntry(address_t address)
			{
				return speedMap.count(address) == 0 ? 0 : speedMap[address];
			}

			unsigned char GetFunctionMapEntry(address_t address)
			{
				return functionMap.count(address) == 0 ? 0 : functionMap[address];
			}

			void SendTwoBytes(const unsigned char byte1, const unsigned char byte2)
			{
				char dataArray[2];
				dataArray[0] = byte1;
				dataArray[1] = byte2;
				std::string data(dataArray, 2);
				serialLine.Send(data);
			}

			void S88Worker();
	};

	extern "C" M6051* create_m6051(const HardwareParams* params);
	extern "C" void destroy_m6051(M6051* m6051);

} // namespace
