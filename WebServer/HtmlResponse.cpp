#include <sstream>

#include "WebServer/HtmlResponse.h"

namespace WebServer
{
	HtmlResponse::HtmlResponse(const responseCode_t responseCode, const std::string& title, const HtmlTag body)
	:	Response(responseCode, body),
	 	title(title)
	{
		AddHeader("Cache-Control", "no-cache, must-revalidate");
		AddHeader("Pragma", "no-cache");
		AddHeader("Expires", "Sun, 12 Feb 2016 00:00:00 GMT");
		AddHeader("Content-Type", "text/html; charset=utf-8");
		AddHeader("Connection", "keep-alive");
	}

	void HtmlResponse::AddAttribute(const std::string name, const std::string value)
	{
		this->content.AddAttribute(name, value);
	}

	void HtmlResponse::AddChildTag(HtmlTag content)
	{
		this->content.AddChildTag(content);
	}

	HtmlResponse::operator std::string()
	{
		std::stringstream reply;
		reply << *this;
		return reply.str();
	}

	std::ostream& operator<<(std::ostream& stream, const HtmlResponse& response)
	{
		stream << "HTTP/1.1 " << response.responseCode << " " << HtmlResponse::responseTexts.at(response.responseCode) << "\r\n";
		for(auto header : response.headers)
		{
			stream << header.first << ": " << header.second << "\r\n";
		}

		std::stringstream body;
		body << "<!DOCTYPE html>";

		HtmlTag html("html");
		if (response.title.length() > 0)
		{
			HtmlTag head("head");
			head.AddChildTag(HtmlTag("title").AddContent(response.title));
			html.AddChildTag(head);
		}
		html.AddChildTag(response.content);
		body << html;

		std::string bodyString(body.str());
		stream << "Content-Length: " << bodyString.size();
		stream << "\r\n\r\n";
		stream << bodyString;
		return stream;
	}
};