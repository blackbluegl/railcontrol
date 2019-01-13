#pragma once

#include <string>

#include "webserver/HtmlTag.h"
#include "webserver/HtmlTagLabel.h"
#include "webserver/HtmlTagInputInteger.h"

namespace webserver
{
	class HtmlTagInputIntegerWithLabel : public HtmlTag
	{
		public:
			HtmlTagInputIntegerWithLabel(const std::string& name, const std::string& label, const int min, const int max)
			:	HtmlTagInputIntegerWithLabel(name, label, 0, min, max)
			{}

			HtmlTagInputIntegerWithLabel(const std::string& name, const std::string& label, const int value, const int min, const int max)
			:	HtmlTag()
			{
				AddChildTag(HtmlTagLabel(label, name));
				AddChildTag(HtmlTagInputInteger(name, value, min, max));
			}

			virtual HtmlTag AddAttribute(const std::string& name, const std::string& value)
			{
				childTags[1].AddAttribute(name, value);
				return *this;
			}

			virtual HtmlTag AddClass(const std::string& _class)
			{
				childTags[1].AddClass(_class);
				return *this;
			}
	};
};

