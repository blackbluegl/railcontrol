#include <dlfcn.h>              // dl*
#include <sstream>

#include "util.h"
#include "storage_handler.h"

using datamodel::Accessory;
using datamodel::Block;
using datamodel::Feedback;
using datamodel::Loco;
using std::map;

namespace storage {

	StorageHandler::StorageHandler(const StorageParams& params) :
		createStorage(NULL),
		destroyStorage(NULL),
		instance(NULL),
		dlhandle(NULL) {

		// generate symbol and library names
		char* error;
		std::stringstream ss;
		ss << "storage/" << params.module << ".so";

		dlhandle = dlopen(ss.str().c_str(), RTLD_LAZY);
		if (!dlhandle) {
			xlog("Can not open storage library: %s", dlerror());
			return;
		}

		// look for symbol create_*
		ss.str(std::string());
		ss << "create_" << params.module;
		const char* s = ss.str().c_str();
		createStorage_t* newCreateStorage = (createStorage_t*) dlsym(dlhandle, s);
		error = dlerror();
		if (error) {
			xlog("Unable to find symbol %s", s);
			return;
		}

		// look for symbol destroy_*
		ss.str(std::string());
		ss << "destroy_" << params.module;
		s = ss.str().c_str();
		destroyStorage_t* newDestroyStorage = (destroyStorage_t*) dlsym(dlhandle,
		    ss.str().c_str());
		error = dlerror();
		if (error) {
			xlog("Unable to find symbol %s", s);
			return;
		}

		// register  valid symbols
		createStorage = newCreateStorage;
		destroyStorage = newDestroyStorage;

		// start storage
		if (createStorage) {
			instance = createStorage(params);
		}
	}

	StorageHandler::~StorageHandler() {
		// stop storage
		if (instance) {
			destroyStorage(instance);
			instance = NULL;
		}
		// close library
		if (dlhandle) {
			dlclose(dlhandle);
			dlhandle = NULL;
		}
	}

	void StorageHandler::hardwareParams(const hardware::HardwareParams& hardwareParams) {
		if (instance) {
			instance->hardwareParams(hardwareParams);
		}
	}

	void StorageHandler::allHardwareParams(std::map<controlID_t,hardware::HardwareParams*>& hardwareParams) {
		if (instance) {
			instance->allHardwareParams(hardwareParams);
		}
	}

	void StorageHandler::loco(const Loco& loco) {
		if (instance) {
			instance->loco(loco);
		}
	}

	void StorageHandler::allLocos(map<locoID_t,datamodel::Loco*>& locos) {
		if (instance) {
			return instance->allLocos(locos);
		}
	}

	void StorageHandler::accessory(const Accessory& accessory) {
		if (instance) {
			instance->accessory(accessory);
		}
	}

	void StorageHandler::allAccessories(std::map<accessoryID_t,datamodel::Accessory*>& accessories) {
		if (instance) {
			return instance->allAccessories(accessories);
		}
	}

	void StorageHandler::feedback(const Feedback& feedback) {
		if (instance) {
			instance->feedback(feedback);
		}
	}

	void StorageHandler::allFeedbacks(std::map<feedbackID_t,datamodel::Feedback*>& feedbacks) {
		if (instance) {
			return instance->allFeedbacks(feedbacks);
		}
	}

	void StorageHandler::allBlocks(std::map<blockID_t,datamodel::Block*>& blocks) {
		if (instance) {
			return instance->allBlocks(blocks);
		}
	}

	void StorageHandler::block(const Block& block) {
		if (instance) {
			instance->block(block);
		}
	}

} // namespace storage

