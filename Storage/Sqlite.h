#pragma once

#include <map>

#include "DataModel/DataModel.h"
#include "Logger/Logger.h"
#include "Storage/sqlite/sqlite3.h"
#include "Storage/StorageInterface.h"
#include "Storage/StorageParams.h"

namespace Storage
{
	class SQLite : public StorageInterface
	{
		public:
			SQLite(const StorageParams& params);
			~SQLite();

			void SaveHardwareParams(const Hardware::HardwareParams& params) override;
			void AllHardwareParams(std::map<controlID_t,Hardware::HardwareParams*>& hardwareParams) override;
			void DeleteHardwareParams(const controlID_t controlID) override;
			void SaveObject(const objectType_t objectType, const objectID_t objectID, const std::string& name, const std::string& object) override;
			void DeleteObject(const objectType_t objectType, const objectID_t objectID) override;
			void ObjectsOfType(const objectType_t objectType, std::vector<std::string>& objects) override;
			void SaveRelation(const objectType_t objectType1, const objectID_t objectID1, const objectType_t objectType2, const objectID_t objectID2, const priority_t priority, const std::string& relation) override;
			void DeleteRelationFrom(const objectType_t objectType, const objectID_t objectID) override;
			void DeleteRelationTo(const objectType_t objectType, const objectID_t objectID) override;
			void RelationsFrom(const objectType_t objectType, const objectID_t objectID, std::vector<std::string>& relations) override;
			void RelationsTo(const objectType_t objectType, const objectID_t objectID, std::vector<std::string>& relations) override;
			void SaveSetting(const std::string& key, const std::string& value) override;
			std::string GetSetting(const std::string& key) override;
			void StartTransaction() override;
			void CommitTransaction() override;

		private:
			void Execute(const std::string& query);

			sqlite3 *db;
			const std::string filename;
			Logger::Logger* logger;

			static int CallbackListTables(void *v, int argc, char **argv, char **colName);
			static int CallbackAllHardwareParams(void *v, int argc, char **argv, char **colName);
			static int CallbackStringVector(void* v, int argc, char **argv, char **colName);
	};

	extern "C" SQLite* create_Sqlite(const StorageParams& params);
	extern "C" void destroy_Sqlite(SQLite* sqlite);

} // namespace Storage
