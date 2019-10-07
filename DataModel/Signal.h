#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "DataModel/Accessory.h"
#include "DataTypes.h"

class Manager;

namespace DataModel
{
	class Signal : public Accessory
	{
		public:
			enum signalType : signalType_t
			{
				SignalTypeSimple = 0
			};

			enum signalState : signalState_t
			{
				SignalStateRed = false,
				SignalStateGreen = true
			};

			Signal(Manager* manager, const signalID_t signalID)
			:	Accessory(manager, signalID),
			 	manager(manager)
			{
			}

			Signal(Manager* manager, const std::string& serialized)
			:	manager(manager)
			{
				Deserialize(serialized);
			}

			objectType_t GetObjectType() const { return ObjectTypeSignal; }

			std::string Serialize() const override;
			bool Deserialize(const std::string& serialized) override;
			std::string LayoutType() const override { return "signal"; };

			virtual bool Release(const locoID_t locoID) override;

			signalState_t GetState() const { return static_cast<signalState_t>(state); }
			signalType_t GetType() const { return static_cast<signalType_t>(type); }

			static void Status(const signalState_t state, std::string& stateText)
			{
				stateText.assign(state == DataModel::Signal::SignalStateGreen ? "green" : "red");
			}

		private:
			Manager* manager;
	};
} // namespace DataModel
