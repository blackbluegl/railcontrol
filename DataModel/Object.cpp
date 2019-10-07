#include <map>
#include <sstream>
#include <string>

#include "DataModel/Object.h"
#include "Utils/Utils.h"

using std::map;
using std::stringstream;
using std::string;

namespace DataModel
{
	std::string Object::Serialize() const
	{
		stringstream ss;
		ss << "objectID=" << (int) objectID << ";name=" << name;
		return ss.str();
	}

	bool Object::Deserialize(const std::string& serialized)
	{
		map<string, string> arguments;
		ParseArguments(serialized, arguments);
		return Deserialize(arguments);
	}

	bool Object::Deserialize(const map<string, string>& arguments)
	{
		objectID = Utils::Utils::GetIntegerMapEntry(arguments, "objectID", ObjectNone);
		name = Utils::Utils::GetStringMapEntry(arguments, "name");
		return true;
	}

} // namespace DataModel
