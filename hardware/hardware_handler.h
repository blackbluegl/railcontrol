#ifndef HARDWARE_HARDWARE_HANDLER_H
#define HARDWARE_HARDWARE_HANDLER_H

#include <string>

#include "datatypes.h"
#include "hardware_interface.h"
#include "hardware_params.h"
#include "manager.h"
#include "manager_interface.h"
#include "util.h"

namespace hardware {

	// the types of the class factories
	typedef hardware::HardwareInterface* createHardware_t(const hardware::HardwareParams* params);
	typedef void destroyHardware_t(hardware::HardwareInterface*);

	class HardwareHandler: public ManagerInterface {
		public:
			HardwareHandler(const Manager& manager, const HardwareParams* params);
			~HardwareHandler();
			controlID_t getControlID();
			std::string getName() const;
			void go(const managerID_t managerID) override;
			void stop(const managerID_t managerID) override;
			void locoSpeed(const managerID_t managerID, const locoID_t locoID, const speed_t speed) override;
			void locoFunction(const managerID_t managerID, const locoID_t locoID, const function_t function, const bool on) override;
			void feedback(const managerID_t managerID, const feedbackPin_t pin, const feedbackState_t state) override;
		private:
			const Manager& manager;
			createHardware_t* createHardware;
			destroyHardware_t* destroyHardware;
			hardware::HardwareInterface* instance;
			void* dlhandle;
			const HardwareParams* params;
	};

	inline void HardwareHandler::feedback(const managerID_t managerID, const feedbackPin_t pin, const feedbackState_t state) {};
};
// namespace hardware

#endif // HARDWARE_HARDWARE_HANDLER_H

