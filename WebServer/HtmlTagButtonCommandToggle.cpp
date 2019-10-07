#include <sstream>
#include <vector>

#include "Utils/Utils.h"
#include "WebServer/HtmlTagButtonCommandToggle.h"

namespace WebServer
{
	HtmlTagButtonCommandToggle::HtmlTagButtonCommandToggle(const std::string& value, const std::string& command, const bool on, const std::map<std::string,std::string>& arguments)
	:	HtmlTagButton(value, command)
	{
		AddClass(on == true ? "button_on" : "button_off");

		std::vector<std::string> parts;
		Utils::Utils::SplitString(command, "_", parts);
		std::stringstream ss;
		ss <<
			"var on = !document.getElementById('" << commandID << "').classList.contains('button_on');"
			"var theUrl = '/?cmd=" << parts[0] << "&on=' + on + '";

		for (auto argument : arguments) {

			ss << "&" << argument.first << "=" << argument.second;
		}
		ss <<"';"
			"fireRequestAndForget(theUrl);"
			"return false;";
		AddAttribute("onclick", ss.str());
	}
};