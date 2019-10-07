
#ifndef AMALGAMATION
#include <dlfcn.h>              // dl*
#endif

#include <sstream>

#include "DataTypes.h"
#include "Logger/Logger.h"
#include "Hardware/HardwareHandler.h"
#include "Hardware/CS2.h"
#include "Hardware/M6051.h"
#include "Hardware/RM485.h"
#include "Hardware/Virtual.h"
#include "Utils/Utils.h"

using std::string;

namespace Hardware
{
	const std::string HardwareHandler::hardwareSymbols[] =
	{
		"none",
		"Virtual",
		"CS2",
		"M6051",
		"RM485",
		"OpenDcc"
	};

	HardwareHandler::HardwareHandler(Manager& manager, const HardwareParams* params)
	:	ControlInterface(ControlTypeHardware),
		manager(manager),
		createHardware(nullptr),
		destroyHardware(nullptr),
		instance(nullptr),
		params(params)
	{
		hardwareType_t type = params->hardwareType;

#ifdef AMALGAMATION
		switch(type)
		{
			case HardwareTypeCS2:
				createHardware = (Hardware::HardwareInterface* (*)(const Hardware::HardwareParams*))(&create_cs2);
				destroyHardware = (void (*)(Hardware::HardwareInterface*))(&destroy_cs2);
				break;

			case HardwareTypeVirtual:
				createHardware = (Hardware::HardwareInterface* (*)(const Hardware::HardwareParams*))(&create_virtual);
				destroyHardware = (void (*)(Hardware::HardwareInterface*))(&destroy_virtual);
				break;


			case HardwareTypeM6051:
				createHardware = (Hardware::HardwareInterface* (*)(const Hardware::HardwareParams*))(&create_m6051);
				destroyHardware = (void (*)(Hardware::HardwareInterface*))(&destroy_m6051);
				break;

			case HardwareTypeRM485:
				createHardware = (Hardware::HardwareInterface* (*)(const Hardware::HardwareParams*))(&create_rm485);
				destroyHardware = (void (*)(Hardware::HardwareInterface*))(&destroy_rm485);
				break;

			case HardwareTypeOpenDcc:
				createHardware = (Hardware::HardwareInterface* (*)(const Hardware::HardwareParams*))(&create_OpenDcc);
				destroyHardware = (void (*)(Hardware::HardwareInterface*))(&destroy_OpenDcc);
				break;

			default:
				createHardware = nullptr;
				destroyHardware = nullptr;
				break;
		}
#else
		// FIXME: if the same hardware library is loaded twice
		// FIXME: the clean up does not work correctly
		// FIXME: the second unload will crash

		// generate symbol and library names
		char* error;
		const string& symbol = hardwareSymbols[type];
		std::stringstream ss;
		ss << "hardware/" << symbol << ".so";

		Logger::Logger* logger = Logger::Logger::GetLogger("HardwareHandler");
		void* dlhandle = manager.HardwareLibraryGet(type);
		if (dlhandle == nullptr)
		{
			// open dynamic library
			dlhandle = dlopen(ss.str().c_str(), RTLD_LAZY);
			if (!dlhandle)
			{
				logger->Error("Can not open library: {0}", dlerror());
				return;
			}
			logger->Info("Hardware library {0} loaded", symbol);
			if (!manager.HardwareLibraryAdd(type, dlhandle))
			{
				logger->Error("Unable to store library address");
				return;
			}
		}

		// look for symbol create_*
		ss.str(std::string());
		ss << "create_" << symbol;
		string s = ss.str();
		createHardware_t* new_create_hardware = (createHardware_t*)dlsym(dlhandle, s.c_str());
		error = dlerror();
		if (error)
		{
			logger->Error("Unable to find symbol {0}: {1}", s, error);
			return;
		}

		// look for symbol destroy_*
		ss.str(std::string());
		ss << "destroy_" << symbol;
		s = ss.str();
		destroyHardware_t* new_destroy_hardware = (destroyHardware_t*)dlsym(dlhandle, s.c_str());
		error = dlerror();
		if (error)
		{
			logger->Error("Unable to find symbol {0}: {1}", s, error);
			return;
		}

		// register  valid symbols
		createHardware = new_create_hardware;
		destroyHardware = new_destroy_hardware;
#endif

		// start control
		if (createHardware)
		{
			instance = createHardware(params);
		}
	}

	HardwareHandler::~HardwareHandler()
	{
		// stop control
		if (instance)
		{
			destroyHardware(instance);
			instance = nullptr;
		}

#ifndef AMALGAMAGTION
		hardwareType_t type = params->hardwareType;
		// close library
		if (manager.ControlsOfHardwareType(type) > 1)
		{
			return;
		}
		void* dlhandle = manager.HardwareLibraryGet(type);
		if (dlhandle == nullptr)
		{
			return;
		}
		if (manager.HardwareLibraryRemove(type) == false)
		{
			return;
		}
		dlclose(dlhandle);
		Logger::Logger::GetLogger("HardwareHandler")->Info("Hardware library {0} unloaded", hardwareSymbols[type]);
#endif
	}

	const std::string HardwareHandler::GetName() const
	{
		if (instance == nullptr)
		{
			return "Unknown, not running";
		}
		return instance->GetName();
	}

	bool HardwareHandler::CanHandleLocos() const
	{
		if (instance == nullptr)
		{
			return false;
		}

		return instance->CanHandleLocos();
	}

	bool HardwareHandler::CanHandleAccessories() const
	{
		if (instance == nullptr)
		{
			return false;
		}

		return instance->CanHandleAccessories();
	}

	bool HardwareHandler::CanHandleFeedback() const
	{
		if (instance == nullptr)
		{
			return false;
		}

		return instance->CanHandleFeedback();
	}

	void HardwareHandler::LocoProtocols(std::vector<protocol_t>& protocols) const
	{
		if (instance == nullptr)
		{
			protocols.push_back(ProtocolNone);
			return;
		}

		instance->GetLocoProtocols(protocols);
	}

	bool HardwareHandler::LocoProtocolSupported(protocol_t protocol) const
	{
		if (instance == nullptr)
		{
			return false;
		}
		return instance->LocoProtocolSupported(protocol);
	}

	void HardwareHandler::AccessoryProtocols(std::vector<protocol_t>& protocols) const
	{
		if (instance == nullptr)
		{
			protocols.push_back(ProtocolNone);
			return;
		}

		instance->GetAccessoryProtocols(protocols);
	}

	bool HardwareHandler::AccessoryProtocolSupported(protocol_t protocol) const
	{
		if (instance == nullptr)
		{
			return false;
		}
		return instance->AccessoryProtocolSupported(protocol);
	}

	void HardwareHandler::ArgumentTypes(std::map<unsigned char,argumentType_t>& argumentTypes) const
	{
		if (instance == nullptr)
		{
			return;
		}
		instance->GetArgumentTypes(argumentTypes);
	}

	void HardwareHandler::Booster(const controlType_t controlType, const boosterState_t status)
	{
		if (controlType == ControlTypeHardware || instance == nullptr)
		{
			return;
		}
		instance->Booster(status);
	}

	void HardwareHandler::LocoSpeed(const controlType_t controlType, const locoID_t locoID, const locoSpeed_t speed)
	{
		if (controlType == ControlTypeHardware || instance == nullptr)
		{
			return;
		}
		controlID_t controlID = 0;
		protocol_t protocol = ProtocolNone;
		address_t address = AddressNone;
		manager.LocoProtocolAddress(locoID, controlID, protocol, address);
		if (controlID != ControlID())
		{
			return;
		}
		instance->LocoSpeed(protocol, address, speed);
	}

	void HardwareHandler::LocoDirection(const controlType_t controlType, const locoID_t locoID, const direction_t direction)
	{
		if (controlType == ControlTypeHardware || instance == nullptr)
		{
			return;
		}
		controlID_t controlID = 0;
		protocol_t protocol = ProtocolNone;
		address_t address = AddressNone;
		manager.LocoProtocolAddress(locoID, controlID, protocol, address);
		if (controlID != ControlID())
		{
			return;
		}
		instance->LocoDirection(protocol, address, direction);
	}

	void HardwareHandler::LocoFunction(const controlType_t controlType, const locoID_t locoID, const function_t function, const bool on)
	{
		if (controlType == ControlTypeHardware || instance == nullptr)
		{
			return;
		}
		controlID_t controlID = 0;
		protocol_t protocol = ProtocolNone;
		address_t address = AddressNone;
		manager.LocoProtocolAddress(locoID, controlID, protocol, address);
		if (controlID != ControlID())
		{
			return;
		}
		instance->LocoFunction(protocol, address, function, on);
	}

	void HardwareHandler::AccessoryState(const controlType_t controlType, const accessoryID_t accessoryID, const accessoryState_t state, const bool on)
	{
		if (controlType == ControlTypeHardware || instance == nullptr)
		{
			return;
		}
		controlID_t controlID = 0;
		protocol_t protocol = ProtocolNone;
		address_t address = AddressNone;
		manager.AccessoryProtocolAddress(accessoryID, controlID, protocol, address);
		if (controlID != ControlID())
		{
			return;
		}
		instance->Accessory(protocol, address, state, on);
	}

	void HardwareHandler::SwitchState(const controlType_t controlType, const switchID_t switchID, const switchState_t state, const bool on)
	{
		if (controlType == ControlTypeHardware || instance == nullptr)
		{
			return;
		}
		controlID_t controlID = 0;
		protocol_t protocol = ProtocolNone;
		address_t address = AddressNone;
		manager.SwitchProtocolAddress(switchID, controlID, protocol, address);
		if (controlID != ControlID())
		{
			return;
		}
		instance->Accessory(protocol, address, state, on);
	}

	void HardwareHandler::SignalState(const controlType_t controlType, const signalID_t signalID, const signalState_t state, const bool on)
	{
		if (controlType == ControlTypeHardware || instance == nullptr)
		{
			return;
		}
		controlID_t controlID = 0;
		protocol_t protocol = ProtocolNone;
		address_t address = AddressNone;
		manager.SignalProtocolAddress(signalID, controlID, protocol, address);
		if (controlID != ControlID())
		{
			return;
		}
		instance->Accessory(protocol, address, state, on);
	}
} // namespace Hardware