#pragma once

#include <mutex>
#include <string>
#include <thread>

#include "datatypes.h"
#include "object.h"

class Manager;

namespace datamodel
{
	class Loco : public Object
	{
		public:
			Loco(Manager* manager, const locoID_t locoID, const std::string& name, const controlID_t controlID, const protocol_t protocol, const address_t address);
			Loco(Manager* manager, const std::string& serialized);
			~Loco();

			std::string serialize() const override;
			bool deserialize(const std::string& serialized) override;

			bool start();
			bool stop();

			bool toBlock(const blockID_t blockID);
			bool toBlock(const blockID_t blockIDOld, const blockID_t blockIDNew);
			bool release();
			blockID_t block() const { return blockID; }
			streetID_t street() const { return streetID; }
			const char* const getStateText() const;
			void destinationReached();

			void Speed(const speed_t speed) { this->speed = speed; }
			const speed_t Speed() const { return speed; }

			bool isInUse() const;

			// FIXME: make private:
			controlID_t controlID;
			protocol_t protocol;
			address_t address;

		private:
			enum locoState_t : unsigned char
			{
				LocoStateManual = 0,
				LocoStateOff,
				LocoStateSearching,
				LocoStateRunning,
				LocoStateStopping,
				LocoStateError
			};

			Manager* manager;
			speed_t speed;
			locoState_t state;
			blockID_t blockID;
			streetID_t streetID;
			std::mutex stateMutex;
			std::thread locoThread;

			void autoMode(Loco* loco);
	};

	inline bool Loco::isInUse() const
	{
		return this->speed > 0 || this->state != LocoStateManual || this->blockID != BlockNone || this->streetID != StreetNone;
	}

} // namespace datamodel
