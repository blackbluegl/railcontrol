/*
RailControl - Model Railway Control Software

Copyright (c) 2017-2019 Dominik (Teddy) Mahrer - www.railcontrol.org

RailControl is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

RailControl is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RailControl; see the file LICENCE. If not see
<http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cstring>		//memset
#include <netinet/in.h>
#include <signal.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "DataModel/DataModel.h"
#include "Hardware/HardwareHandler.h"
#include "RailControl.h"
#include "Timestamp.h"
#include "Utils/Utils.h"
#include "WebServer/HtmlFullResponse.h"
#include "WebServer/HtmlResponse.h"
#include "WebServer/HtmlResponseNotFound.h"
#include "WebServer/HtmlResponseNotImplemented.h"
#include "WebServer/HtmlTagAccessory.h"
#include "WebServer/HtmlTagButtonCancel.h"
#include "WebServer/HtmlTagButtonCommand.h"
#include "WebServer/HtmlTagButtonCommandToggle.h"
#include "WebServer/HtmlTagButtonCommandWide.h"
#include "WebServer/HtmlTagButtonOK.h"
#include "WebServer/HtmlTagButtonPopup.h"
#include "WebServer/HtmlTagButtonPopupWide.h"
#include "WebServer/HtmlTagFeedback.h"
#include "WebServer/HtmlTagInputCheckboxWithLabel.h"
#include "WebServer/HtmlTagInputHidden.h"
#include "WebServer/HtmlTagInputIntegerWithLabel.h"
#include "WebServer/HtmlTagInputSliderLocoSpeed.h"
#include "WebServer/HtmlTagInputTextWithLabel.h"
#include "WebServer/HtmlTagSelectWithLabel.h"
#include "WebServer/HtmlTagSignal.h"
#include "WebServer/HtmlTagStreet.h"
#include "WebServer/HtmlTagSwitch.h"
#include "WebServer/HtmlTagTrack.h"
#include "WebServer/WebClient.h"
#include "WebServer/WebServer.h"

using DataModel::Accessory;
using DataModel::Feedback;
using DataModel::Layer;
using DataModel::Loco;
using DataModel::Relation;
using DataModel::Signal;
using DataModel::Street;
using DataModel::Switch;
using DataModel::Track;
using std::map;
using std::string;
using std::stringstream;
using std::thread;
using std::to_string;
using std::vector;

namespace WebServer
{
	WebClient::~WebClient()
	{
		run = false;
		clientThread.join();
		connection->Terminate();
	}

	// worker is the thread that handles client requests
	void WebClient::Worker()
	{
		Utils::Utils::SetThreadName("WebClient");
		logger->Info(Languages::TextHttpConnectionOpen, id);
		WorkerImpl();
		logger->Info(Languages::TextHttpConnectionClose, id);
	}

	void WebClient::WorkerImpl()
	{
		run = true;
		bool keepalive = true;

		while (run && keepalive)
		{
			char buffer_in[1024];
			memset(buffer_in, 0, sizeof(buffer_in));

			size_t pos = 0;
			string s;
			while (pos < sizeof(buffer_in) - 1 && s.find("\n\n") == string::npos && run)
			{
				size_t ret = connection->Receive(buffer_in + pos, sizeof(buffer_in) - 1 - pos, 0);
				if (ret == static_cast<size_t>(-1))
				{
					if (errno != ETIMEDOUT)
					{
						return;
					}
					if (run == false)
					{
						return;
					}
					continue;
				}
				pos += ret;
				s = string(buffer_in);
				Utils::Utils::ReplaceString(s, string("\r\n"), string("\n"));
				Utils::Utils::ReplaceString(s, string("\r"), string("\n"));
			}

			vector<string> lines;
			Utils::Utils::SplitString(s, string("\n"), lines);

			if (lines.size() <= 1)
			{
				return;
			}

			string method;
			string uri;
			string protocol;
			map<string, string> arguments;
			map<string, string> headers;
			InterpretClientRequest(lines, method, uri, protocol, arguments, headers);
			keepalive = (Utils::Utils::GetStringMapEntry(headers, "Connection", "close").compare("keep-alive") == 0);
			logger->Info(Languages::TextHttpConnectionRequest, id, method, uri);

			// if method is not implemented
			if ((method.compare("GET") != 0) && (method.compare("HEAD") != 0))
			{
				logger->Info(Languages::TextHttpConnectionNotImplemented, id, method);
				HtmlResponseNotImplemented response(method);
				connection->Send(response);
				return;
			}

			// handle requests
			if (arguments["cmd"].compare("quit") == 0)
			{
				ReplyHtmlWithHeaderAndParagraph(Languages::TextStoppingRailControl);
				stopRailControlWebserver();
			}
			else if (arguments["cmd"].compare("booster") == 0)
			{
				bool on = Utils::Utils::GetBoolMapEntry(arguments, "on");
				if (on)
				{
					ReplyHtmlWithHeaderAndParagraph(Languages::TextTurningBoosterOn);
					manager.Booster(ControlTypeWebserver, BoosterGo);
				}
				else
				{
					ReplyHtmlWithHeaderAndParagraph(Languages::TextTurningBoosterOff);
					manager.Booster(ControlTypeWebserver, BoosterStop);
				}
			}
			else if (arguments["cmd"].compare("layeredit") == 0)
			{
				HandleLayerEdit(arguments);
			}
			else if (arguments["cmd"].compare("layersave") == 0)
			{
				HandleLayerSave(arguments);
			}
			else if (arguments["cmd"].compare("layerlist") == 0)
			{
				HandleLayerList();
			}
			else if (arguments["cmd"].compare("layeraskdelete") == 0)
			{
				HandleLayerAskDelete(arguments);
			}
			else if (arguments["cmd"].compare("layerdelete") == 0)
			{
				HandleLayerDelete(arguments);
			}
			else if (arguments["cmd"].compare("controledit") == 0)
			{
				HandleControlEdit(arguments);
			}
			else if (arguments["cmd"].compare("controlsave") == 0)
			{
				HandleControlSave(arguments);
			}
			else if (arguments["cmd"].compare("controllist") == 0)
			{
				HandleControlList();
			}
			else if (arguments["cmd"].compare("controlaskdelete") == 0)
			{
				HandleControlAskDelete(arguments);
			}
			else if (arguments["cmd"].compare("controldelete") == 0)
			{
				HandleControlDelete(arguments);
			}
			else if (arguments["cmd"].compare("loco") == 0)
			{
				PrintLoco(arguments);
			}
			else if (arguments["cmd"].compare("locospeed") == 0)
			{
				HandleLocoSpeed(arguments);
			}
			else if (arguments["cmd"].compare("locodirection") == 0)
			{
				HandleLocoDirection(arguments);
			}
			else if (arguments["cmd"].compare("locofunction") == 0)
			{
				HandleLocoFunction(arguments);
			}
			else if (arguments["cmd"].compare("locoedit") == 0)
			{
				HandleLocoEdit(arguments);
			}
			else if (arguments["cmd"].compare("locosave") == 0)
			{
				HandleLocoSave(arguments);
			}
			else if (arguments["cmd"].compare("locolist") == 0)
			{
				HandleLocoList();
			}
			else if (arguments["cmd"].compare("locoaskdelete") == 0)
			{
				HandleLocoAskDelete(arguments);
			}
			else if (arguments["cmd"].compare("locodelete") == 0)
			{
				HandleLocoDelete(arguments);
			}
			else if (arguments["cmd"].compare("locorelease") == 0)
			{
				HandleLocoRelease(arguments);
			}
			else if (arguments["cmd"].compare("accessoryedit") == 0)
			{
				HandleAccessoryEdit(arguments);
			}
			else if (arguments["cmd"].compare("accessorysave") == 0)
			{
				HandleAccessorySave(arguments);
			}
			else if (arguments["cmd"].compare("accessorystate") == 0)
			{
				HandleAccessoryState(arguments);
			}
			else if (arguments["cmd"].compare("accessorylist") == 0)
			{
				HandleAccessoryList();
			}
			else if (arguments["cmd"].compare("accessoryaskdelete") == 0)
			{
				HandleAccessoryAskDelete(arguments);
			}
			else if (arguments["cmd"].compare("accessorydelete") == 0)
			{
				HandleAccessoryDelete(arguments);
			}
			else if (arguments["cmd"].compare("accessoryget") == 0)
			{
				HandleAccessoryGet(arguments);
			}
			else if (arguments["cmd"].compare("accessoryrelease") == 0)
			{
				HandleAccessoryRelease(arguments);
			}
			else if (arguments["cmd"].compare("switchedit") == 0)
			{
				HandleSwitchEdit(arguments);
			}
			else if (arguments["cmd"].compare("switchsave") == 0)
			{
				HandleSwitchSave(arguments);
			}
			else if (arguments["cmd"].compare("switchstate") == 0)
			{
				HandleSwitchState(arguments);
			}
			else if (arguments["cmd"].compare("switchlist") == 0)
			{
				HandleSwitchList();
			}
			else if (arguments["cmd"].compare("switchaskdelete") == 0)
			{
				HandleSwitchAskDelete(arguments);
			}
			else if (arguments["cmd"].compare("switchdelete") == 0)
			{
				HandleSwitchDelete(arguments);
			}
			else if (arguments["cmd"].compare("switchget") == 0)
			{
				HandleSwitchGet(arguments);
			}
			else if (arguments["cmd"].compare("switchrelease") == 0)
			{
				HandleSwitchRelease(arguments);
			}
			else if (arguments["cmd"].compare("signaledit") == 0)
			{
				HandleSignalEdit(arguments);
			}
			else if (arguments["cmd"].compare("signalsave") == 0)
			{
				HandleSignalSave(arguments);
			}
			else if (arguments["cmd"].compare("signalstate") == 0)
			{
				HandleSignalState(arguments);
			}
			else if (arguments["cmd"].compare("signallist") == 0)
			{
				HandleSignalList();
			}
			else if (arguments["cmd"].compare("signalaskdelete") == 0)
			{
				HandleSignalAskDelete(arguments);
			}
			else if (arguments["cmd"].compare("signaldelete") == 0)
			{
				HandleSignalDelete(arguments);
			}
			else if (arguments["cmd"].compare("signalget") == 0)
			{
				HandleSignalGet(arguments);
			}
			else if (arguments["cmd"].compare("signalrelease") == 0)
			{
				HandleSignalRelease(arguments);
			}
			else if (arguments["cmd"].compare("streetedit") == 0)
			{
				HandleStreetEdit(arguments);
			}
			else if (arguments["cmd"].compare("streetsave") == 0)
			{
				HandleStreetSave(arguments);
			}
			else if (arguments["cmd"].compare("streetlist") == 0)
			{
				HandleStreetList();
			}
			else if (arguments["cmd"].compare("streetaskdelete") == 0)
			{
				HandleStreetAskDelete(arguments);
			}
			else if (arguments["cmd"].compare("streetdelete") == 0)
			{
				HandleStreetDelete(arguments);
			}
			else if (arguments["cmd"].compare("streetget") == 0)
			{
				HandleStreetGet(arguments);
			}
			else if (arguments["cmd"].compare("streetexecute") == 0)
			{
				HandleStreetExecute(arguments);
			}
			else if (arguments["cmd"].compare("streetrelease") == 0)
			{
				HandleStreetRelease(arguments);
			}
			else if (arguments["cmd"].compare("trackedit") == 0)
			{
				HandleTrackEdit(arguments);
			}
			else if (arguments["cmd"].compare("tracksave") == 0)
			{
				HandleTrackSave(arguments);
			}
			else if (arguments["cmd"].compare("tracklist") == 0)
			{
				HandleTrackList();
			}
			else if (arguments["cmd"].compare("trackaskdelete") == 0)
			{
				HandleTrackAskDelete(arguments);
			}
			else if (arguments["cmd"].compare("trackdelete") == 0)
			{
				HandleTrackDelete(arguments);
			}
			else if (arguments["cmd"].compare("trackget") == 0)
			{
				HandleTrackGet(arguments);
			}
			else if (arguments["cmd"].compare("tracksetloco") == 0)
			{
				HandleTrackSetLoco(arguments);
			}
			else if (arguments["cmd"].compare("trackrelease") == 0)
			{
				HandleTrackRelease(arguments);
			}
			else if (arguments["cmd"].compare("trackstartloco") == 0)
			{
				HandleTrackStartLoco(arguments);
			}
			else if (arguments["cmd"].compare("trackstoploco") == 0)
			{
				HandleTrackStopLoco(arguments);
			}
			else if (arguments["cmd"].compare("trackblock") == 0)
			{
				HandleTrackBlock(arguments);
			}
			else if (arguments["cmd"].compare("feedbackedit") == 0)
			{
				HandleFeedbackEdit(arguments);
			}
			else if (arguments["cmd"].compare("feedbacksave") == 0)
			{
				HandleFeedbackSave(arguments);
			}
			else if (arguments["cmd"].compare("feedbackstate") == 0)
			{
				HandleFeedbackState(arguments);
			}
			else if (arguments["cmd"].compare("feedbacklist") == 0)
			{
				HandleFeedbackList();
			}
			else if (arguments["cmd"].compare("feedbackaskdelete") == 0)
			{
				HandleFeedbackAskDelete(arguments);
			}
			else if (arguments["cmd"].compare("feedbackdelete") == 0)
			{
				HandleFeedbackDelete(arguments);
			}
			else if (arguments["cmd"].compare("feedbackget") == 0)
			{
				HandleFeedbackGet(arguments);
			}
			else if (arguments["cmd"].compare("feedbacksoftrack") == 0)
			{
				HandleFeedbacksOfTrack(arguments);
			}
			else if (arguments["cmd"].compare("protocolloco") == 0)
			{
				HandleProtocolLoco(arguments);
			}
			else if (arguments["cmd"].compare("protocolaccessory") == 0)
			{
				HandleProtocolAccessory(arguments);
			}
			else if (arguments["cmd"].compare("protocolswitch") == 0)
			{
				HandleProtocolSwitch(arguments);
			}
			else if (arguments["cmd"].compare("feedbackadd") == 0)
			{
				HandleFeedbackAdd(arguments);
			}
			else if (arguments["cmd"].compare("relationadd") == 0)
			{
				HandleRelationAdd(arguments);
			}
			else if (arguments["cmd"].compare("relationobject") == 0)
			{
				HandleRelationObject(arguments);
			}
			else if (arguments["cmd"].compare("layout") == 0)
			{
				HandleLayout(arguments);
			}
			else if (arguments["cmd"].compare("locoselector") == 0)
			{
				HandleLocoSelector();
			}
			else if (arguments["cmd"].compare("layerselector") == 0)
			{
				HandleLayerSelector();
			}
			else if (arguments["cmd"].compare("stopallimmediately") == 0)
			{
				manager.StopAllLocosImmediately(ControlTypeWebserver);
			}
			else if (arguments["cmd"].compare("startall") == 0)
			{
				manager.LocoStartAll();
			}
			else if (arguments["cmd"].compare("stopall") == 0)
			{
				manager.LocoStopAll();
			}
			else if (arguments["cmd"].compare("settingsedit") == 0)
			{
				HandleSettingsEdit();
			}
			else if (arguments["cmd"].compare("settingssave") == 0)
			{
				HandleSettingsSave(arguments);
			}
			else if (arguments["cmd"].compare("slaveadd") == 0)
			{
				HandleSlaveAdd(arguments);
			}
			else if (arguments["cmd"].compare("timestamp") == 0)
			{
				HandleTimestamp(arguments);
			}
			else if (arguments["cmd"].compare("controlarguments") == 0)
			{
				HandleControlArguments(arguments);
			}
			else if (arguments["cmd"].compare("updater") == 0)
			{
				HandleUpdater(headers);
			}
			else if (uri.compare("/") == 0)
			{
				PrintMainHTML();
			}
			else
			{
				DeliverFile(uri);
			}
		}
	}

	int WebClient::Stop()
	{
		// inform working thread to stop
		run = false;
		return 0;
	}

	char WebClient::ConvertHexToInt(char c)
	{
		if (c >= 'a')
		{
			c -= 'a' - 10;
		}
		else if (c >= 'A')
		{
			c -= 'A' - 10;
		}
		else if (c >= '0')
		{
			c -= '0';
		}

		if (c > 15)
		{
			return 0;
		}

		return c;
	}

	void WebClient::UrlDecode(string& argumentValue)
	{
		// decode %20 and similar
		while (true)
		{
			size_t pos = argumentValue.find('%');
			if (pos == string::npos || pos + 3 > argumentValue.length())
			{
				break;
			}
			char c = ConvertHexToInt(argumentValue[pos + 1]) * 16 + ConvertHexToInt(argumentValue[pos + 2]);
			argumentValue.replace(pos, 3, 1, c);
		}
	}

	void WebClient::InterpretClientRequest(const vector<string>& lines, string& method, string& uri, string& protocol, map<string,string>& arguments, map<string,string>& headers)
	{
		if (lines.size() == 0)
		{
			return;
		}

		for (auto line : lines)
		{
			if (line.find("HTTP/1.") == string::npos)
			{
				vector<string> list;
				Utils::Utils::SplitString(line, string(": "), list);
				if (list.size() == 2)
				{
					headers[list[0]] = list[1];
				}
				continue;
			}

			vector<string> list;
			Utils::Utils::SplitString(line, string(" "), list);
			if (list.size() != 3)
			{
				continue;
			}

			method = list[0];
			// transform method to uppercase
			std::transform(method.begin(), method.end(), method.begin(), ::toupper);

			// if method == HEAD set membervariable
			headOnly = method.compare("HEAD") == 0;

			// set uri and protocol
			uri = list[1];
			UrlDecode(uri);
			protocol = list[2];

			// read GET-arguments from uri
			vector<string> uri_parts;
			Utils::Utils::SplitString(uri, "?", uri_parts);
			if (uri_parts.size() != 2)
			{
				continue;
			}

			vector<string> argumentStrings;
			Utils::Utils::SplitString(uri_parts[1], "&", argumentStrings);
			for (auto argument : argumentStrings)
			{
				if (argument.length() == 0)
				{
					continue;
				}
				vector<string> argumentParts;
				Utils::Utils::SplitString(argument, "=", argumentParts);
				arguments[argumentParts[0]] = argumentParts[1];
			}
		}
	}

	void WebClient::DeliverFile(const string& virtualFile)
	{
		stringstream ss;
		char workingDir[128];
		if (getcwd(workingDir, sizeof(workingDir)))
		{
			ss << workingDir << "/html" << virtualFile;
		}
		string sFile = ss.str();
		const char* realFile = sFile.c_str();
		FILE* f = fopen(realFile, "r");
		if (f == nullptr)
		{
			HtmlResponseNotFound response(virtualFile);
			connection->Send(response);
			logger->Info(Languages::TextHttpConnectionNotFound, id, virtualFile);
			return;
		}

		DeliverFileInternal(f, realFile, virtualFile);
		fclose(f);
	}

	void WebClient::DeliverFileInternal(FILE* f, const char* realFile, const string& virtualFile)
	{
		struct stat s;
		int rc = stat(realFile, &s);
		if (rc != 0)
		{
			return;
		}

		size_t length = virtualFile.length();
		const char* contentType = nullptr;
		if (length > 4 && virtualFile[length - 4] == '.')
		{
			if (virtualFile[length - 3] == 'i' && virtualFile[length - 2] == 'c' && virtualFile[length - 1] == 'o')
			{
				contentType = "image/x-icon";
			}
			else if (virtualFile[length - 3] == 'c' && virtualFile[length - 2] == 's' && virtualFile[length - 1] == 's')
			{
				contentType = "text/css";
			}
			else if (virtualFile[length - 3] == 'p' && virtualFile[length - 2] == 'n' && virtualFile[length - 1] == 'g')
			{
				contentType = "image/png";
			}
			else if (virtualFile[length - 3] == 't' && virtualFile[length - 2] == 't' && virtualFile[length - 1] == 'f')
			{
				contentType = "application/x-font-ttf";
			}
		}
		else if (length > 3 && virtualFile[length - 3] == '.' && virtualFile[length - 2] == 'j' && virtualFile[length - 1] == 's')
		{
			contentType = "application/javascript";
		}

		Response response;
		response.AddHeader("Cache-Control", "max-age=3600");
		response.AddHeader("Content-Length", to_string(s.st_size));
		if (contentType != nullptr)
		{
			response.AddHeader("Content-Type", contentType);
		}
		connection->Send(response);

		if (headOnly == true)
		{
			return;
		}

		char* buffer = static_cast<char*>(malloc(s.st_size));
		if (buffer == nullptr)
		{
			return;
		}

		size_t r = fread(buffer, 1, s.st_size, f);
		connection->Send(buffer, r, 0);
		free(buffer);
	}

	HtmlTag WebClient::HtmlTagControlArgument(const unsigned char argNr, const argumentType_t type, const string& value)
	{
		Languages::textSelector_t argumentName;
		string argumentNumber = "arg" + to_string(argNr);
		switch (type)
		{
			case IpAddress:
				argumentName = Languages::TextIPAddress;
				break;

			case SerialPort:
				argumentName = Languages::TextSerialPort;
				break;

			case S88Modules:
			{
				argumentName = Languages::TextNrOfS88Modules;
				const int valueInteger = Utils::Utils::StringToInteger(value, 0, 62);
				return HtmlTagInputIntegerWithLabel(argumentNumber, argumentName, valueInteger, 0, 62);
			}

			default:
				return HtmlTag();
		}
		return HtmlTagInputTextWithLabel(argumentNumber, argumentName, value);
	}

	void WebClient::HandleLayerEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		layerID_t layerID = Utils::Utils::GetIntegerMapEntry(arguments, "layer", LayerNone);
		string name = Languages::GetText(Languages::TextNew);

		if (layerID != LayerNone)
		{
			Layer* layer = manager.GetLayer(layerID);
			if (layer != nullptr)
			{
				name = layer->GetName();
			}
		}

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddAttribute("id", "popup_title"));
		HtmlTag form("form");
		form.AddAttribute("id", "editform");
		form.AddChildTag(HtmlTagInputHidden("cmd", "layersave"));
		form.AddChildTag(HtmlTagInputHidden("layer", to_string(layerID)));
		form.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(form));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleLayerSave(const map<string, string>& arguments)
	{
		layerID_t layerID = Utils::Utils::GetIntegerMapEntry(arguments, "layer", LayerNone);
		string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		string result;

		if (!manager.LayerSave(layerID, name, result))
		{
			ReplyResponse(ResponseError, result);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextLayerSaved, name);
	}

	void WebClient::HandleLayerAskDelete(const map<string, string>& arguments)
	{
		layerID_t layerID = Utils::Utils::GetIntegerMapEntry(arguments, "layer", LayerNone);

		if (layerID == LayerNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextLayerDoesNotExist);
			return;
		}

		if (layerID == LayerUndeletable)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextLayer1IsUndeletable);
			return;
		}

		const Layer* layer = manager.GetLayer(layerID);
		if (layer == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::GetText(Languages::TextLayerDoesNotExist));
			return;
		}

		HtmlTag content;
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteLayer));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, layer->GetName()));
		content.AddContent(HtmlTag("form").AddAttribute("id", "editform")
			.AddContent(HtmlTagInputHidden("cmd", "layerdelete"))
			.AddContent(HtmlTagInputHidden("layer", to_string(layerID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleLayerDelete(const map<string, string>& arguments)
	{
		layerID_t layerID = Utils::Utils::GetIntegerMapEntry(arguments, "layer", LayerNone);

		if (layerID == LayerNone)
		{
			ReplyResponse(ResponseError, Languages::TextLayerDoesNotExist);
			return;
		}

		if (layerID == LayerUndeletable)
		{
			ReplyResponse(ResponseError, Languages::TextLayer1IsUndeletable);
			return;
		}

		const Layer* layer = manager.GetLayer(layerID);
		if (layer == nullptr)
		{
			ReplyResponse(ResponseError, Languages::TextLayerDoesNotExist);
			return;
		}

		string name = layer->GetName();

		if (!manager.LayerDelete(layerID))
		{
			ReplyResponse(ResponseError, Languages::TextLayerDoesNotExist);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextLayerDeleted);
	}

	void WebClient::HandleLayerList()
	{
		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextLayers));
		HtmlTag table("table");
		const map<string,layerID_t> layerList = manager.LayerListByName();
		map<string,string> layerArgument;
		for (auto layer : layerList)
		{
			HtmlTag row("tr");
			row.AddChildTag(HtmlTag("td").AddContent(layer.first));
			string layerIdString = to_string(layer.second);
			layerArgument["layer"] = layerIdString;
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextEdit, "layeredit_list_" + layerIdString, layerArgument)));
			if (layer.second != LayerUndeletable)
			{
				row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextDelete, "layeraskdelete_" + layerIdString, layerArgument)));
			}
			table.AddChildTag(row);
		}
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(table));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonPopupWide(Languages::TextNew, "layeredit_0"));
		ReplyHtmlWithHeader(content);
	}

	HtmlTag WebClient::HtmlTagControlArguments(const hardwareType_t hardwareType, const string& arg1, const string& arg2, const string& arg3, const string& arg4, const string& arg5)
	{
		HtmlTag div;
		std::map<unsigned char,argumentType_t> argumentTypes;
		Hardware::HardwareHandler::ArgumentTypesOfHardwareType(hardwareType, argumentTypes);
		if (argumentTypes.count(1) == 1)
		{
			div.AddChildTag(HtmlTagControlArgument(1, argumentTypes.at(1), arg1));
		}
		if (argumentTypes.count(2) == 1)
		{
			div.AddChildTag(HtmlTagControlArgument(2, argumentTypes.at(2), arg2));
		}
		if (argumentTypes.count(3) == 1)
		{
			div.AddChildTag(HtmlTagControlArgument(3, argumentTypes.at(3), arg3));
		}
		if (argumentTypes.count(4) == 1)
		{
			div.AddChildTag(HtmlTagControlArgument(4, argumentTypes.at(4), arg4));
		}
		if (argumentTypes.count(5) == 1)
		{
			div.AddChildTag(HtmlTagControlArgument(5, argumentTypes.at(5), arg5));
		}
		return div;
	}
	void WebClient::HandleControlEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		controlID_t controlID = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		hardwareType_t hardwareType = HardwareTypeNone;
		string name = Languages::GetText(Languages::TextNew);
		string arg1;
		string arg2;
		string arg3;
		string arg4;
		string arg5;

		if (controlID != ControlIdNone)
		{
			Hardware::HardwareParams* params = manager.GetHardware(controlID);
			if (params != nullptr)
			{
				hardwareType = params->hardwareType;
				name = params->name;
				arg1 = params->arg1;
				arg2 = params->arg2;
				arg3 = params->arg3;
				arg4 = params->arg4;
				arg5 = params->arg5;
			}
		}

		const std::map<hardwareType_t,string> hardwares = manager.HardwareListNames();
		std::map<string, string> hardwareOptions;
		for(auto hardware : hardwares)
		{
			hardwareOptions[to_string(hardware.first)] = hardware.second;
		}

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddAttribute("id", "popup_title"));
		HtmlTag form("form");
		form.AddAttribute("id", "editform");
		form.AddChildTag(HtmlTagInputHidden("cmd", "controlsave"));
		form.AddChildTag(HtmlTagInputHidden("control", to_string(controlID)));
		form.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));

		HtmlTagSelectWithLabel selectHardwareType("hardwaretype", Languages::TextType, hardwareOptions, to_string(hardwareType));
		selectHardwareType.AddAttribute("onchange", "getArgumentsOfHardwareType();");
		form.AddChildTag(selectHardwareType);

		HtmlTag controlArguments("div");
		controlArguments.AddAttribute("id", "controlarguments");
		controlArguments.AddChildTag(HtmlTagControlArguments(hardwareType, arg1, arg2, arg3, arg4, arg5));
		form.AddChildTag(controlArguments);

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(form));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleControlSave(const map<string, string>& arguments)
	{
		controlID_t controlID = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		hardwareType_t hardwareType = static_cast<hardwareType_t>(Utils::Utils::GetIntegerMapEntry(arguments, "hardwaretype", HardwareTypeNone));
		string arg1 = Utils::Utils::GetStringMapEntry(arguments, "arg1");
		string arg2 = Utils::Utils::GetStringMapEntry(arguments, "arg2");
		string arg3 = Utils::Utils::GetStringMapEntry(arguments, "arg3");
		string arg4 = Utils::Utils::GetStringMapEntry(arguments, "arg4");
		string arg5 = Utils::Utils::GetStringMapEntry(arguments, "arg5");
		string result;

		if (!manager.ControlSave(controlID, hardwareType, name, arg1, arg2, arg3, arg4, arg5, result))
		{
			ReplyResponse(ResponseError, result);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextControlSaved, name);
	}

	void WebClient::HandleControlAskDelete(const map<string, string>& arguments)
	{
		controlID_t controlID = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlNone);

		if (controlID == ControlNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextControlDoesNotExist);
			return;
		}

		const Hardware::HardwareParams* control = manager.GetHardware(controlID);
		if (control == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextControlDoesNotExist);
			return;
		}

		HtmlTag content;
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteControl));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, control->name));
		content.AddContent(HtmlTag("form").AddAttribute("id", "editform")
			.AddContent(HtmlTagInputHidden("cmd", "controldelete"))
			.AddContent(HtmlTagInputHidden("control", to_string(controlID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleControlDelete(const map<string, string>& arguments)
	{
		controlID_t controlID = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlNone);
		const Hardware::HardwareParams* control = manager.GetHardware(controlID);
		if (control == nullptr)
		{
			ReplyResponse(ResponseError, Languages::TextControlDoesNotExist);
			return;
		}

		string name = control->name;

		if (!manager.ControlDelete(controlID))
		{
			ReplyResponse(ResponseError, Languages::TextControlDoesNotExist);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextControlDeleted, name);
	}

	void WebClient::HandleControlList()
	{
		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent("Controls"));
		HtmlTag table("table");
		const map<string,Hardware::HardwareParams*> hardwareList = manager.ControlListByName();
		map<string,string> hardwareArgument;
		for (auto hardware : hardwareList)
		{
			HtmlTag row("tr");
			row.AddChildTag(HtmlTag("td").AddContent(hardware.first));
			string controlIdString = to_string(hardware.second->controlID);
			hardwareArgument["control"] = controlIdString;
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextEdit, "controledit_list_" + controlIdString, hardwareArgument)));
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextDelete, "controlaskdelete_" + controlIdString, hardwareArgument)));
			table.AddChildTag(row);
		}
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(table));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonPopupWide(Languages::TextNew, "controledit_0"));
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleLocoSpeed(const map<string, string>& arguments)
	{
		locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		locoSpeed_t speed = Utils::Utils::GetIntegerMapEntry(arguments, "speed", MinSpeed);

		manager.LocoSpeed(ControlTypeWebserver, locoID, speed);

		ReplyHtmlWithHeaderAndParagraph(Languages::TextLocoSpeedIs, manager.GetLocoName(locoID), speed);
	}

	void WebClient::HandleLocoDirection(const map<string, string>& arguments)
	{
		locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		direction_t direction = (Utils::Utils::GetBoolMapEntry(arguments, "on") ? DirectionRight : DirectionLeft);

		manager.LocoDirection(ControlTypeWebserver, locoID, direction);

		ReplyHtmlWithHeaderAndParagraph(direction == DirectionLeft ? Languages::TextLocoDirectionIsLeft : Languages::TextLocoDirectionIsRight, manager.GetLocoName(locoID));
	}

	void WebClient::HandleLocoFunction(const map<string, string>& arguments)
	{
		locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		function_t function = Utils::Utils::GetIntegerMapEntry(arguments, "function", 0);
		bool on = Utils::Utils::GetBoolMapEntry(arguments, "on");

		manager.LocoFunction(ControlTypeWebserver, locoID, function, on);

		ReplyHtmlWithHeaderAndParagraph(on ? Languages::TextLocoFunctionIsOn : Languages::TextLocoFunctionIsOff, manager.GetLocoName(locoID), function);
	}

	void WebClient::HandleLocoRelease(const map<string, string>& arguments)
	{
		bool ret;
		locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		if (locoID == LocoNone)
		{
			trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track", TrackNone);
			ret = manager.LocoReleaseInTrack(trackID);
		}
		else
		{
			ret = manager.LocoRelease(locoID);
		}
		ReplyHtmlWithHeaderAndParagraph(ret ? "Loco released" : "Loco not released");
	}

	HtmlTag WebClient::HtmlTagProtocolLoco(const controlID_t controlID, const protocol_t selectedProtocol)
	{
		HtmlTag content;
		content.AddChildTag(HtmlTagLabel(Languages::TextProtocol, "protocol"));
		map<string,protocol_t> protocolMap = manager.LocoProtocolsOfControl(controlID);
		content.AddChildTag(HtmlTagSelect("protocol", protocolMap, selectedProtocol));
		return content;
	}

	void WebClient::HandleProtocolLoco(const map<string, string>& arguments)
	{
		controlID_t controlId = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		if (controlId == ControlIdNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextControlDoesNotExist);
			return;
		}
		locoID_t locoId = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		Loco* loco = manager.GetLoco(locoId);
		ReplyHtmlWithHeader(HtmlTagProtocolLoco(controlId, loco == nullptr ? ProtocolNone : loco->GetProtocol()));
	}

	HtmlTag WebClient::HtmlTagProtocolAccessory(const controlID_t controlID, const protocol_t selectedProtocol)
	{
		HtmlTag content;
		content.AddChildTag(HtmlTagLabel(Languages::TextProtocol, "protocol"));
		map<string,protocol_t> protocolMap = manager.AccessoryProtocolsOfControl(controlID);
		content.AddChildTag(HtmlTagSelect("protocol", protocolMap, selectedProtocol));
		return content;
	}

	HtmlTag WebClient::HtmlTagDuration(const accessoryDuration_t duration, const Languages::textSelector_t label) const
	{
		std::map<string,string> durationOptions;
		durationOptions["0000"] = "0";
		durationOptions["0100"] = "100";
		durationOptions["0250"] = "250";
		durationOptions["1000"] = "1000";
		return HtmlTagSelectWithLabel("duration", label, durationOptions, Utils::Utils::ToStringWithLeadingZeros(duration, 4));
	}

	HtmlTag WebClient::HtmlTagPosition(const layoutPosition_t posx, const layoutPosition_t posy, const layoutPosition_t posz)
	{
		HtmlTag content("div");
		content.AddAttribute("id", "position");;
		content.AddChildTag(HtmlTagInputIntegerWithLabel("posx", Languages::TextPosX, posx, 0, 255));
		content.AddChildTag(HtmlTagInputIntegerWithLabel("posy", Languages::TextPosY, posy, 0, 255));
		map<string,layerID_t> layerList = manager.LayerListByName();
		content.AddChildTag(HtmlTagSelectWithLabel("posz", Languages::TextPosZ, layerList, posz));
		return content;
	}

	HtmlTag WebClient::HtmlTagPosition(const layoutPosition_t posx, const layoutPosition_t posy, const layoutPosition_t posz, const visible_t visible)
	{
		HtmlTag content;
		HtmlTagInputCheckboxWithLabel checkboxVisible("visible", Languages::TextVisible, "visible", static_cast<bool>(visible));
		checkboxVisible.AddAttribute("id", "visible");
		checkboxVisible.AddAttribute("onchange", "onChangeCheckboxShowHide('visible', 'position');");
		content.AddChildTag(checkboxVisible);
		HtmlTag posDiv = HtmlTagPosition(posx, posy, posz);
		if (visible == VisibleNo)
		{
			posDiv.AddAttribute("hidden");
		}
		content.AddChildTag(posDiv);
		return content;
	}

	HtmlTag WebClient::HtmlTagRelationObject(const string& priority, const objectType_t objectType, const objectID_t objectId, const accessoryState_t state)
	{
		HtmlTag content;
		switch (objectType)
		{
			case ObjectTypeSwitch:
			{
				std::map<string, Switch*> switches = manager.SwitchListByName();
				map<string, switchID_t> switchOptions;
				for (auto mySwitch : switches)
				{
					switchOptions[mySwitch.first] = mySwitch.second->GetID();
				}
				content.AddChildTag(HtmlTagSelect("relation_id_" + priority, switchOptions, objectId).AddClass("select_relation_id"));

				map<switchState_t,Languages::textSelector_t> stateOptions;
				stateOptions[DataModel::Switch::SwitchStateStraight] = Languages::TextStraight;
				stateOptions[DataModel::Switch::SwitchStateTurnout] = Languages::TextTurnout;
				content.AddChildTag(HtmlTagSelect("relation_state_" + priority, stateOptions, state).AddClass("select_relation_state"));
				return content;
			}

			case ObjectTypeSignal:
			{
				std::map<string, Signal*> signals = manager.SignalListByName();
				map<string, signalID_t> signalOptions;
				for (auto signal : signals)
				{
					signalOptions[signal.first] = signal.second->GetID();
				}
				content.AddChildTag(HtmlTagSelect("relation_id_" + priority, signalOptions, objectId).AddClass("select_relation_id"));

				map<signalState_t,Languages::textSelector_t> stateOptions;
				stateOptions[DataModel::Signal::SignalStateGreen] = Languages::TextGreen;
				stateOptions[DataModel::Signal::SignalStateRed] = Languages::TextRed;
				content.AddChildTag(HtmlTagSelect("relation_state_" + priority, stateOptions, state).AddClass("select_relation_state"));
				return content;
			}

			case ObjectTypeAccessory:
			{
				std::map<string, Accessory*> accessories = manager.AccessoryListByName();
				map<string, accessoryID_t> accessoryOptions;
				for (auto accessory : accessories)
				{
					accessoryOptions[accessory.first] = accessory.second->GetID();
				}
				content.AddChildTag(HtmlTagSelect("relation_id_" + priority, accessoryOptions, objectId).AddClass("select_relation_id"));

				map<accessoryState_t,Languages::textSelector_t> stateOptions;
				stateOptions[DataModel::Accessory::AccessoryStateOn] = Languages::TextOn;
				stateOptions[DataModel::Accessory::AccessoryStateOff] = Languages::TextOff;
				content.AddChildTag(HtmlTagSelect("relation_state_" + priority, stateOptions, state).AddClass("select_relation_state"));
				return content;
			}

			case ObjectTypeTrack:
			{
				std::map<string, Track*> tracks = manager.TrackListByName();
				map<string, trackID_t> trackOptions;
				for (auto track : tracks)
				{
					trackOptions[track.first] = track.second->GetID();
				}
				content.AddChildTag(HtmlTagSelect("relation_id_" + priority, trackOptions, objectId).AddClass("select_relation_id"));

				map<unsigned char,Languages::textSelector_t> stateOptions;
				stateOptions[static_cast<unsigned char>(DirectionLeft)] = Languages::TextLeft;
				stateOptions[static_cast<unsigned char>(DirectionRight)] = Languages::TextRight;
				content.AddChildTag(HtmlTagSelect("relation_state_" + priority, stateOptions, static_cast<unsigned char>(state)).AddClass("select_relation_state"));
				return content;
			}

			case ObjectTypeStreet:
			{
				std::map<string, Street*> streets = manager.StreetListByName();
				map<string, streetID_t> streetOptions;
				for (auto track : streets)
				{
					streetOptions[track.first] = track.second->GetID();
				}
				content.AddChildTag(HtmlTagSelect("relation_id_" + priority, streetOptions, objectId).AddClass("select_relation_id"));
				return content;
			}

			default:
			{
				content.AddContent(Languages::TextUnknownObjectType);
				return content;
			}
		}
	}

	HtmlTag WebClient::HtmlTagRelation(const string& priority, const objectType_t objectType, const objectID_t objectId, const accessoryState_t state)
	{
		HtmlTag content("div");
		content.AddAttribute("id", "priority_" + priority);
		HtmlTagButton deleteButton(Languages::TextDelete, "delete_relation_" + priority);
		deleteButton.AddAttribute("onclick", "deleteElement('priority_" + priority + "');return false;");
		deleteButton.AddClass("wide_button");
		content.AddChildTag(deleteButton);

		map<objectType_t,Languages::textSelector_t> objectTypeOptions;
		objectTypeOptions[ObjectTypeAccessory] = Languages::TextAccessory;
		objectTypeOptions[ObjectTypeSignal] = Languages::TextSignal;
		objectTypeOptions[ObjectTypeSwitch] = Languages::TextSwitch;
		objectTypeOptions[ObjectTypeTrack] = Languages::TextTrack;
		objectTypeOptions[ObjectTypeStreet] = Languages::TextStreet;
		HtmlTagSelect select("relation_type_" + priority, objectTypeOptions, objectType);
		select.AddClass("select_relation_objecttype");
		select.AddAttribute("onchange", "loadRelationObject(" + priority + ");return false;");
		content.AddChildTag(select);
		HtmlTag contentObject("div");
		contentObject.AddAttribute("id", "relation_object_" + priority);
		contentObject.AddClass("inline-block");
		contentObject.AddChildTag(HtmlTagRelationObject(priority, objectType, objectId, state));
		content.AddChildTag(contentObject);
		return content;
	}

	HtmlTag WebClient::HtmlTagSlave(const string& priority, const objectID_t objectId)
	{
		HtmlTag content("div");
		content.AddAttribute("id", "priority_" + priority);
		HtmlTagButton deleteButton(Languages::TextDelete, "delete_slave_" + priority);
		deleteButton.AddAttribute("onclick", "deleteElement('priority_" + priority + "');return false;");
		deleteButton.AddClass("wide_button");
		content.AddChildTag(deleteButton);

		HtmlTag contentObject("div");
		contentObject.AddAttribute("id", "slave_object_" + priority);
		contentObject.AddClass("inline-block");

		std::map<string, Loco*> locos = manager.LocoListByName();
		map<string, switchID_t> locoOptions;
		for (auto loco : locos)
		{
			locoOptions[loco.first] = loco.second->GetID();
		}
		contentObject.AddChildTag(HtmlTagSelect("slave_id_" + priority, locoOptions, objectId).AddClass("select_slave_id"));
		content.AddChildTag(contentObject);
		return content;
	}

	HtmlTag WebClient::HtmlTagSelectFeedbackForTrack(const unsigned int counter, const trackID_t trackID, const feedbackID_t feedbackID)
	{
		string counterString = to_string(counter);
		HtmlTag content("div");
		content.AddAttribute("id", "feedback_container_" + counterString);
		HtmlTagButton deleteButton(Languages::TextDelete, "delete_feedback_" + counterString);
		deleteButton.AddAttribute("onclick", "deleteElement('feedback_container_" + counterString + "');return false;");
		deleteButton.AddClass("wide_button");
		content.AddChildTag(deleteButton);

		map<string, Feedback*> feedbacks = manager.FeedbackListByName();
		map<string, feedbackID_t> feedbackOptions;
		for (auto feedback : feedbacks)
		{
			const trackID_t trackIDOfFeedback = feedback.second->GetTrack();
			if (trackIDOfFeedback == TrackNone || trackIDOfFeedback == trackID)
			{
				feedbackOptions[feedback.first] = feedback.second->GetID();
			}
		}
		content.AddChildTag(HtmlTagSelect("feedback_" + counterString, feedbackOptions, feedbackID));
		content.AddChildTag(HtmlTag("div").AddAttribute("id", "div_feedback_" + to_string(counter + 1)));
		return content;
	}

	HtmlTag WebClient::HtmlTagRotation(const DataModel::LayoutItem::layoutRotation_t rotation) const
	{
		std::map<DataModel::LayoutItem::layoutRotation_t, Languages::textSelector_t> rotationOptions;
		rotationOptions[DataModel::LayoutItem::Rotation0] = Languages::TextNoRotation;
		rotationOptions[DataModel::LayoutItem::Rotation90] = Languages::Text90DegClockwise;
		rotationOptions[DataModel::LayoutItem::Rotation180] = Languages::Text180Deg;
		rotationOptions[DataModel::LayoutItem::Rotation270] = Languages::Text90DegAntiClockwise;
		return HtmlTagSelectWithLabel("rotation", Languages::TextRotation, rotationOptions, rotation);
	}

	HtmlTag WebClient::HtmlTagSelectTrack(const std::string& name, const Languages::textSelector_t label, const trackID_t trackId, const direction_t direction, const string& onchange) const
	{
		HtmlTag tag;
		map<string,trackID_t> tracks = manager.TrackListIdByName();
		HtmlTagSelectWithLabel selectTrack(name + "track", label, tracks, trackId);
		selectTrack.AddClass("select_track");
		if (onchange.size() > 0)
		{
			selectTrack.AddAttribute("onchange", onchange);
		}
		tag.AddChildTag(selectTrack);
		map<direction_t,Languages::textSelector_t> directions;
		directions[DirectionLeft] = Languages::TextLeft;
		directions[DirectionRight] = Languages::TextRight;
		tag.AddChildTag(HtmlTagSelect(name + "direction", directions, direction).AddClass("select_direction"));
		return tag;
	}

	HtmlTag WebClient::HtmlTagSelectFeedbacksOfTrack(const trackID_t trackId, const feedbackID_t feedbackIdReduced, const feedbackID_t feedbackIdCreep, const feedbackID_t feedbackIdStop, const feedbackID_t feedbackIdOver) const
	{
		HtmlTag tag;
		map<string,feedbackID_t> feedbacks = manager.FeedbacksOfTrack(trackId);
		map<string,feedbackID_t> feedbacksWithNone = feedbacks;
		feedbacksWithNone["-"] = FeedbackNone;
		tag.AddChildTag(HtmlTagSelectWithLabel("feedbackreduced", Languages::TextReducedSpeedAt, feedbacksWithNone, feedbackIdReduced).AddClass("select_feedback"));
		tag.AddChildTag(HtmlTagSelectWithLabel("feedbackcreep", Languages::TextCreepAt, feedbacksWithNone, feedbackIdCreep).AddClass("select_feedback"));
		tag.AddChildTag(HtmlTagSelectWithLabel("feedbackstop", Languages::TextStopAt, feedbacks, feedbackIdStop).AddClass("select_feedback"));
		tag.AddChildTag(HtmlTagSelectWithLabel("feedbackover", Languages::TextOverrunAt, feedbacksWithNone, feedbackIdOver).AddClass("select_feedback"));
		return tag;
	}

	HtmlTag WebClient::HtmlTagTabMenuItem(const std::string& tabName, const Languages::textSelector_t buttonValue, const bool selected) const
	{
		HtmlTag button("button");
		button.AddClass("tab_button");
		button.AddAttribute("id", "tab_button_" + tabName);
		button.AddAttribute("onclick", "ShowTab('" + tabName + "');");
		button.AddContent(buttonValue);
		if (selected)
		{
			button.AddClass("tab_button_selected");
		}
		return button;
	}

	HtmlTag WebClient::HtmlTagSelectSelectStreetApproach(const DataModel::Track::selectStreetApproach_t selectStreetApproach, const bool addDefault)
	{
		map<DataModel::Track::selectStreetApproach_t,Languages::textSelector_t> options;
		if (addDefault)
		{
			options[DataModel::Track::SelectStreetSystemDefault] = Languages::TextSystemDefault;
		}
		options[DataModel::Track::SelectStreetDoNotCare] = Languages::TextDoNotCare;
		options[DataModel::Track::SelectStreetRandom] = Languages::TextRandom;
		options[DataModel::Track::SelectStreetMinTrackLength] = Languages::TextMinTrackLength;
		options[DataModel::Track::SelectStreetLongestUnused] = Languages::TextLongestUnused;
		return HtmlTagSelectWithLabel("selectstreetapproach", Languages::TextSelectStreetBy, options, selectStreetApproach);
	}

	HtmlTag WebClient::HtmlTagNrOfTracksToReserve(const DataModel::Loco::nrOfTracksToReserve_t nrOfTracksToReserve)
	{
		map<DataModel::Loco::nrOfTracksToReserve_t,string> options;
		options[DataModel::Loco::ReserveOne] = "1";
		options[DataModel::Loco::ReserveTwo] = "2";
		return HtmlTagSelectWithLabel("nroftrackstoreserve", Languages::TextNrOfTracksToReserve, options, nrOfTracksToReserve);
	}

	void WebClient::HandleProtocolAccessory(const map<string, string>& arguments)
	{
		controlID_t controlId = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		if (controlId == ControlIdNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextControlDoesNotExist);
			return;
		}
		accessoryID_t accessoryId = Utils::Utils::GetIntegerMapEntry(arguments, "accessory", AccessoryNone);
		Accessory* accessory = manager.GetAccessory(accessoryId);
		if (accessory == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextAccessoryDoesNotExist);
			return;
		}
		ReplyHtmlWithHeader(HtmlTagProtocolAccessory(controlId, accessory->GetProtocol()));
	}

	void WebClient::HandleRelationAdd(const map<string, string>& arguments)
	{
		string priorityString = Utils::Utils::GetStringMapEntry(arguments, "priority", "1");
		priority_t priority = Utils::Utils::StringToInteger(priorityString, 1);
		HtmlTag container;
		container.AddChildTag(HtmlTagRelation(priorityString));
		container.AddChildTag(HtmlTag("div").AddAttribute("id", "new_priority_" + to_string(priority + 1)));
		ReplyHtmlWithHeader(container);
	}

	void WebClient::HandleSlaveAdd(const map<string, string>& arguments)
	{
		string priorityString = Utils::Utils::GetStringMapEntry(arguments, "priority", "1");
		priority_t priority = Utils::Utils::StringToInteger(priorityString, 1);
		HtmlTag container;
		container.AddChildTag(HtmlTagSlave(priorityString));
		container.AddChildTag(HtmlTag("div").AddAttribute("id", "new_slave_" + to_string(priority + 1)));
		ReplyHtmlWithHeader(container);
	}

	void WebClient::HandleFeedbackAdd(const map<string, string>& arguments)
	{
		unsigned int counter = Utils::Utils::GetIntegerMapEntry(arguments, "counter", 1);
		trackID_t trackID = static_cast<trackID_t>(Utils::Utils::GetIntegerMapEntry(arguments, "track", TrackNone));
		ReplyHtmlWithHeader(HtmlTagSelectFeedbackForTrack(counter, trackID));
	}

	void WebClient::HandleProtocolSwitch(const map<string, string>& arguments)
	{
		controlID_t controlId = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		if (controlId == ControlIdNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextControlDoesNotExist);
			return;
		}
		switchID_t switchId = Utils::Utils::GetIntegerMapEntry(arguments, "switch", SwitchNone);
		Switch* mySwitch = manager.GetSwitch(switchId);
		if (mySwitch == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextSwitchDoesNotExist);
			return;
		}
		ReplyHtmlWithHeader(HtmlTagProtocolAccessory(controlId, mySwitch->GetProtocol()));
	}

	void WebClient::HandleRelationObject(const map<string, string>& arguments)
	{
		const string priority = Utils::Utils::GetStringMapEntry(arguments, "priority");
		const objectType_t objectType = static_cast<objectType_t>(Utils::Utils::GetIntegerMapEntry(arguments, "objecttype"));
		ReplyHtmlWithHeader(HtmlTagRelationObject(priority, objectType));
	}

	void WebClient::HandleLocoEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		controlID_t controlID = ControlIdNone;
		protocol_t protocol = ProtocolNone;
		address_t address = 1;
		string name = Languages::GetText(Languages::TextNew);
		function_t nrOfFunctions = 0;
		bool pushpull = false;
		length_t length = 0;
		locoSpeed_t maxSpeed = MaxSpeed;
		locoSpeed_t travelSpeed = DefaultTravelSpeed;
		locoSpeed_t reducedSpeed = DefaultReducedSpeed;
		locoSpeed_t creepingSpeed = DefaultCreepingSpeed;
		vector<Relation*> slaves;

		if (locoID > LocoNone)
		{
			const DataModel::Loco* loco = manager.GetLoco(locoID);
			if (loco != nullptr)
			{
				controlID = loco->GetControlID();
				protocol = loco->GetProtocol();
				address = loco->GetAddress();
				name = loco->GetName();
				nrOfFunctions = loco->GetNrOfFunctions();
				pushpull = loco->GetPushpull();
				length = loco->GetLength();
				maxSpeed = loco->GetMaxSpeed();
				travelSpeed = loco->GetTravelSpeed();
				reducedSpeed = loco->GetReducedSpeed();
				creepingSpeed = loco->GetCreepingSpeed();
				slaves = loco->GetSlaves();
			}
		}

		std::map<controlID_t,string> controls = manager.LocoControlListNames();
		std::map<string, string> controlOptions;
		for(auto control : controls)
		{
			controlOptions[to_string(control.first)] = control.second;
			if (controlID == ControlIdNone)
			{
				controlID = control.first;
			}
		}

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddAttribute("id", "popup_title"));
		HtmlTag tabMenu("div");
		tabMenu.AddChildTag(HtmlTagTabMenuItem("basic", Languages::TextBasic, true));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("slaves", Languages::TextMultipleUnit));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("automode", Languages::TextAutomode));
		content.AddChildTag(tabMenu);

		HtmlTag formContent;
		formContent.AddChildTag(HtmlTagInputHidden("cmd", "locosave"));
		formContent.AddChildTag(HtmlTagInputHidden("loco", to_string(locoID)));

		HtmlTag basicContent("div");
		basicContent.AddAttribute("id", "tab_basic");
		basicContent.AddClass("tab_content");
		basicContent.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));
		basicContent.AddChildTag(HtmlTagSelectWithLabel("control", Languages::TextControl, controlOptions, to_string(controlID)).AddAttribute("onchange", "loadProtocol('loco', " + to_string(locoID) + ")"));
		basicContent.AddChildTag(HtmlTag("div").AddAttribute("id", "select_protocol").AddChildTag(HtmlTagProtocolLoco(controlID, protocol)));
		basicContent.AddChildTag(HtmlTagInputIntegerWithLabel("address", Languages::TextAddress, address, 1, 9999));
		basicContent.AddChildTag(HtmlTagInputIntegerWithLabel("function", Languages::TextNrOfFunctions, nrOfFunctions, 0, DataModel::LocoFunctions::maxFunctions));
		basicContent.AddChildTag(HtmlTagInputIntegerWithLabel("length", Languages::TextTrainLength, length, 0, 99999));
		formContent.AddChildTag(basicContent);

		HtmlTag slavesDiv("div");
		slavesDiv.AddChildTag(HtmlTagInputHidden("slavecounter", to_string(slaves.size())));
		slavesDiv.AddAttribute("id", "slaves");
		unsigned int slavecounter = 1;
		for (auto slave : slaves)
		{
			locoID_t slaveID = slave->ObjectID2();
			if (locoID == slaveID)
			{
				continue;
			}
			slavesDiv.AddChildTag(HtmlTagSlave(to_string(slavecounter), slaveID));
			++slavecounter;
		}
		slavesDiv.AddChildTag(HtmlTag("div").AddAttribute("id", "new_slave_" + to_string(slavecounter)));
		HtmlTag relationContent("div");
		relationContent.AddAttribute("id", "tab_slaves");
		relationContent.AddClass("tab_content");
		relationContent.AddClass("hidden");
		relationContent.AddChildTag(slavesDiv);
		HtmlTagButton newButton(Languages::TextNew, "newslave");
		newButton.AddAttribute("onclick", "addSlave();return false;");
		newButton.AddClass("wide_button");
		relationContent.AddChildTag(newButton);
		relationContent.AddChildTag(HtmlTag("br"));
		formContent.AddChildTag(relationContent);

		HtmlTag automodeContent("div");
		automodeContent.AddAttribute("id", "tab_automode");
		automodeContent.AddClass("tab_content");
		automodeContent.AddClass("hidden");
		automodeContent.AddChildTag(HtmlTagInputCheckboxWithLabel("pushpull", Languages::TextPushPullTrain, "pushpull", pushpull));
		automodeContent.AddChildTag(HtmlTagInputIntegerWithLabel("maxspeed", Languages::TextMaxSpeed, maxSpeed, 0, MaxSpeed));
		automodeContent.AddChildTag(HtmlTagInputIntegerWithLabel("travelspeed", Languages::TextTravelSpeed, travelSpeed, 0, MaxSpeed));
		automodeContent.AddChildTag(HtmlTagInputIntegerWithLabel("reducedspeed", Languages::TextReducedSpeed, reducedSpeed, 0, MaxSpeed));
		automodeContent.AddChildTag(HtmlTagInputIntegerWithLabel("creepingspeed", Languages::TextCreepingSpeed, creepingSpeed, 0, MaxSpeed));
		formContent.AddChildTag(automodeContent);

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(HtmlTag("form").AddAttribute("id", "editform").AddChildTag(formContent)));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleLocoSave(const map<string, string>& arguments)
	{
		const locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		const string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		const controlID_t controlId = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		const protocol_t protocol = static_cast<protocol_t>(Utils::Utils::GetIntegerMapEntry(arguments, "protocol", ProtocolNone));
		const address_t address = Utils::Utils::GetIntegerMapEntry(arguments, "address", AddressNone);
		const function_t nrOfFunctions = Utils::Utils::GetIntegerMapEntry(arguments, "function", 0);
		const length_t length = Utils::Utils::GetIntegerMapEntry(arguments, "length", 0);
		const bool pushpull = Utils::Utils::GetBoolMapEntry(arguments, "pushpull", false);
		const locoSpeed_t maxSpeed = Utils::Utils::GetIntegerMapEntry(arguments, "maxspeed", MaxSpeed);
		locoSpeed_t travelSpeed = Utils::Utils::GetIntegerMapEntry(arguments, "travelspeed", DefaultTravelSpeed);
		if (travelSpeed > maxSpeed)
		{
			travelSpeed = maxSpeed;
		}
		locoSpeed_t reducedSpeed = Utils::Utils::GetIntegerMapEntry(arguments, "reducedspeed", DefaultReducedSpeed);
		if (reducedSpeed > travelSpeed)
		{
			reducedSpeed = travelSpeed;
		}
		locoSpeed_t creepingSpeed = Utils::Utils::GetIntegerMapEntry(arguments, "creepingspeed", DefaultCreepingSpeed);
		if (creepingSpeed > reducedSpeed)
		{
			creepingSpeed = reducedSpeed;
		}
		vector<Relation*> slaves;
		unsigned int slaveCount = Utils::Utils::GetIntegerMapEntry(arguments, "slavecounter", 0);
		for (unsigned int index = 1; index <= slaveCount; ++index)
		{
			string slaveString = to_string(index);
			locoID_t slaveId = Utils::Utils::GetIntegerMapEntry(arguments, "slave_id_" + slaveString, LocoNone);
			if (slaveId == LocoNone)
			{
				continue;
			}
			slaves.push_back(new Relation(&manager, ObjectTypeLoco, locoID, ObjectTypeLoco, slaveId, 0, false));
		}

		string result;

		if (!manager.LocoSave(locoID,
			name,
			controlId,
			protocol,
			address,
			nrOfFunctions,
			length,
			pushpull,
			maxSpeed,
			travelSpeed,
			reducedSpeed,
			creepingSpeed,
			slaves,
			result))
		{
			ReplyResponse(ResponseError, result);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextLocoSaved, name);
	}

	void WebClient::HandleLocoList()
	{
		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextLocos));
		HtmlTag table("table");
		const map<string,DataModel::Loco*> locoList = manager.LocoListByName();
		map<string,string> locoArgument;
		for (auto loco : locoList)
		{
			HtmlTag row("tr");
			row.AddChildTag(HtmlTag("td").AddContent(loco.first));
			row.AddChildTag(HtmlTag("td").AddContent(to_string(loco.second->GetAddress())));
			string locoIdString = to_string(loco.second->GetID());
			locoArgument["loco"] = locoIdString;
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextEdit, "locoedit_list_" + locoIdString, locoArgument)));
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextDelete, "locoaskdelete_" + locoIdString, locoArgument)));
			if (loco.second->IsInUse())
			{
				row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonCommandWide(Languages::TextRelease, "locorelease_" + locoIdString, locoArgument, "hideElement('b_locorelease_" + locoIdString + "');")));
			}
			table.AddChildTag(row);
		}
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(table));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonPopupWide(Languages::TextNew, "locoedit_0"));
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleLocoAskDelete(const map<string, string>& arguments)
	{
		locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);

		if (locoID == LocoNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextLocoDoesNotExist);
			return;
		}

		const DataModel::Loco* loco = manager.GetLoco(locoID);
		if (loco == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextLocoDoesNotExist);
			return;
		}

		HtmlTag content;
		const string& locoName = loco->GetName();
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteLoco));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, locoName));
		content.AddContent(HtmlTag("form").AddAttribute("id", "editform")
			.AddContent(HtmlTagInputHidden("cmd", "locodelete"))
			.AddContent(HtmlTagInputHidden("loco", to_string(locoID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleLocoDelete(const map<string, string>& arguments)
	{
		locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		const DataModel::Loco* loco = manager.GetLoco(locoID);
		if (loco == nullptr)
		{
			ReplyResponse(ResponseError, Languages::TextLocoDoesNotExist);
			return;
		}

		string name = loco->GetName();

		if (!manager.LocoDelete(locoID))
		{
			ReplyResponse(ResponseError, Languages::TextLocoDoesNotExist);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextLocoDeleted, name);
	}

	HtmlTag WebClient::HtmlTagLayerSelector() const
	{
		map<string,layerID_t> options = manager.LayerListByNameWithFeedback();
		return HtmlTagSelect("layer", options).AddAttribute("onchange", "loadLayout();");
	}

	void WebClient::HandleLayout(const map<string, string>& arguments)
	{
		layerID_t layer = static_cast<layerID_t>(Utils::Utils::GetIntegerMapEntry(arguments, "layer", CHAR_MIN));
		HtmlTag content;

		if (layer < LayerUndeletable)
		{
			const map<feedbackID_t,Feedback*>& feedbacks = manager.FeedbackList();
			for (auto feedback : feedbacks)
			{
				if (feedback.second->GetControlID() != -layer)
				{
					continue;
				}

				content.AddChildTag(HtmlTagFeedbackOnControlLayer(feedback.second));
			}
			ReplyHtmlWithHeader(content);
			return;
		}

		const map<accessoryID_t,DataModel::Accessory*>& accessories = manager.AccessoryList();
		for (auto accessory : accessories)
		{
			if (accessory.second->IsVisibleOnLayer(layer) == false)
			{
				continue;
			}
			content.AddChildTag(HtmlTagAccessory(accessory.second));
		}

		const map<switchID_t,DataModel::Switch*>& switches = manager.SwitchList();
		for (auto mySwitch : switches)
		{
			if (mySwitch.second->IsVisibleOnLayer(layer) == false)
			{
				continue;
			}
			content.AddChildTag(HtmlTagSwitch(mySwitch.second));
		}

		const map<switchID_t,DataModel::Track*>& tracks = manager.TrackList();
		for (auto track : tracks)
		{
			if (track.second->IsVisibleOnLayer(layer) == false)
			{
				continue;
			}
			content.AddChildTag(HtmlTagTrack(manager, track.second));
		}

		const map<streetID_t,DataModel::Street*>& streets = manager.StreetList();
		for (auto street : streets)
		{
			if (street.second->IsVisibleOnLayer(layer) == false)
			{
				continue;
			}
			content.AddChildTag(HtmlTagStreet(street.second));
		}

		const map<feedbackID_t,Feedback*>& feedbacks = manager.FeedbackList();
		for (auto feedback : feedbacks)
		{
			if (feedback.second->IsVisibleOnLayer(layer) == false)
			{
				continue;
			}
			content.AddChildTag(HtmlTagFeedback(feedback.second));
		}

		const map<signalID_t,DataModel::Signal*>& signals = manager.SignalList();
		for (auto signal : signals)
		{
			if (signal.second->IsVisibleOnLayer(layer) == false)
			{
				continue;
			}
			content.AddChildTag(HtmlTagSignal(signal.second));
		}

		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleAccessoryEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		accessoryID_t accessoryID = Utils::Utils::GetIntegerMapEntry(arguments, "accessory", AccessoryNone);
		controlID_t controlID = ControlIdNone;
		protocol_t protocol = ProtocolNone;
		address_t address = AddressNone;
		string name = Languages::GetText(Languages::TextNew);
		layoutPosition_t posx = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posy = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posz = Utils::Utils::GetIntegerMapEntry(arguments, "posz", LayerUndeletable);
		accessoryDuration_t duration = manager.GetDefaultAccessoryDuration();
		bool inverted = false;
		if (accessoryID > AccessoryNone)
		{
			const DataModel::Accessory* accessory = manager.GetAccessory(accessoryID);
			if (accessory != nullptr)
			{
				controlID = accessory->GetControlID();
				protocol = accessory->GetProtocol();
				address = accessory->GetAddress();
				name = accessory->GetName();
				posx = accessory->GetPosX();
				posy = accessory->GetPosY();
				posz = accessory->GetPosZ();
				duration = accessory->GetDuration();
				inverted = accessory->GetInverted();
			}
		}

		std::map<controlID_t,string> controls = manager.AccessoryControlListNames();
		std::map<string, string> controlOptions;
		for(auto control : controls)
		{
			controlOptions[to_string(control.first)] = control.second;
			if (controlID == ControlIdNone)
			{
				controlID = control.first;
			}
		}

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddAttribute("id", "popup_title"));
		HtmlTag tabMenu("div");
		tabMenu.AddChildTag(HtmlTagTabMenuItem("main", Languages::TextBasic, true));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("position", Languages::TextPosition));
		content.AddChildTag(tabMenu);

		HtmlTag formContent;
		formContent.AddChildTag(HtmlTagInputHidden("cmd", "accessorysave"));
		formContent.AddChildTag(HtmlTagInputHidden("accessory", to_string(accessoryID)));

		HtmlTag mainContent("div");
		mainContent.AddAttribute("id", "tab_main");
		mainContent.AddClass("tab_content");
		mainContent.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));
		mainContent.AddChildTag(HtmlTagSelectWithLabel("control", Languages::TextControl, controlOptions, to_string(controlID)).AddAttribute("onchange", "loadProtocol('accessory', " + to_string(accessoryID) + ")"));
		mainContent.AddChildTag(HtmlTag("div").AddAttribute("id", "select_protocol").AddChildTag(HtmlTagProtocolAccessory(controlID, protocol)));
		mainContent.AddChildTag(HtmlTagInputIntegerWithLabel("address", Languages::TextAddress, address, 1, 2044));
		mainContent.AddChildTag(HtmlTagDuration(duration));
		mainContent.AddChildTag(HtmlTagInputCheckboxWithLabel("inverted", Languages::TextInverted, "true", inverted));
		formContent.AddChildTag(mainContent);

		HtmlTag positionContent("div");
		positionContent.AddAttribute("id", "tab_position");
		positionContent.AddClass("tab_content");
		positionContent.AddClass("hidden");
		positionContent.AddChildTag(HtmlTagPosition(posx, posy, posz));
		formContent.AddChildTag(positionContent);

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(HtmlTag("form").AddAttribute("id", "editform").AddChildTag(formContent)));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleAccessoryGet(const map<string, string>& arguments)
	{
		accessoryID_t accessoryID = Utils::Utils::GetIntegerMapEntry(arguments, "accessory");
		const DataModel::Accessory* accessory = manager.GetAccessory(accessoryID);
		if (accessory == nullptr)
		{
			ReplyHtmlWithHeader(HtmlTag());
			return;
		}
		ReplyHtmlWithHeader(HtmlTagAccessory(accessory));
	}

	void WebClient::HandleAccessorySave(const map<string, string>& arguments)
	{
		accessoryID_t accessoryID = Utils::Utils::GetIntegerMapEntry(arguments, "accessory", AccessoryNone);
		string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		controlID_t controlId = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		protocol_t protocol = static_cast<protocol_t>(Utils::Utils::GetIntegerMapEntry(arguments, "protocol", ProtocolNone));
		address_t address = Utils::Utils::GetIntegerMapEntry(arguments, "address", AddressNone);
		layoutPosition_t posX = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posY = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posZ = Utils::Utils::GetIntegerMapEntry(arguments, "posz", 0);
		accessoryDuration_t duration = Utils::Utils::GetIntegerMapEntry(arguments, "duration", manager.GetDefaultAccessoryDuration());
		bool inverted = Utils::Utils::GetBoolMapEntry(arguments, "inverted");
		string result;
		if (!manager.AccessorySave(accessoryID, name, posX, posY, posZ, controlId, protocol, address, DataModel::Accessory::AccessoryTypeDefault, duration, inverted, result))
		{
			ReplyResponse(ResponseError, result);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextAccessorySaved, name);
	}

	void WebClient::HandleAccessoryState(const map<string, string>& arguments)
	{
		accessoryID_t accessoryID = Utils::Utils::GetIntegerMapEntry(arguments, "accessory", AccessoryNone);
		accessoryState_t accessoryState = (Utils::Utils::GetStringMapEntry(arguments, "state", "off").compare("off") == 0 ? DataModel::Accessory::AccessoryStateOff : DataModel::Accessory::AccessoryStateOn);

		manager.AccessoryState(ControlTypeWebserver, accessoryID, accessoryState, false);

		ReplyHtmlWithHeaderAndParagraph(accessoryState ? Languages::TextAccessoryStateIsGreen : Languages::TextAccessoryStateIsRed, manager.GetAccessoryName(accessoryID));
	}

	void WebClient::HandleAccessoryList()
	{
		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextAccessories));
		HtmlTag table("table");
		const map<string,DataModel::Accessory*> accessoryList = manager.AccessoryListByName();
		map<string,string> accessoryArgument;
		for (auto accessory : accessoryList)
		{
			HtmlTag row("tr");
			row.AddChildTag(HtmlTag("td").AddContent(accessory.first));
			string accessoryIdString = to_string(accessory.second->GetID());
			accessoryArgument["accessory"] = accessoryIdString;
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextEdit, "accessoryedit_list_" + accessoryIdString, accessoryArgument)));
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextDelete, "accessoryaskdelete_" + accessoryIdString, accessoryArgument)));
			if (accessory.second->IsInUse())
			{
				row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonCommandWide(Languages::TextRelease, "accessoryrelease_" + accessoryIdString, accessoryArgument, "hideElement('b_accessoryrelease_" + accessoryIdString + "');")));
			}
			table.AddChildTag(row);
		}
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(table));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonPopupWide(Languages::TextNew, "accessoryedit_0"));
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleAccessoryAskDelete(const map<string, string>& arguments)
	{
		accessoryID_t accessoryID = Utils::Utils::GetIntegerMapEntry(arguments, "accessory", AccessoryNone);

		if (accessoryID == AccessoryNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextAccessoryDoesNotExist);
			return;
		}

		const DataModel::Accessory* accessory = manager.GetAccessory(accessoryID);
		if (accessory == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextAccessoryDoesNotExist);
			return;
		}

		HtmlTag content;
		const string& accessoryName = accessory->GetName();
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteAccessory));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, accessoryName));
		content.AddContent(HtmlTag("form").AddAttribute("id", "editform")
			.AddContent(HtmlTagInputHidden("cmd", "accessorydelete"))
			.AddContent(HtmlTagInputHidden("accessory", to_string(accessoryID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleAccessoryDelete(const map<string, string>& arguments)
	{
		accessoryID_t accessoryID = Utils::Utils::GetIntegerMapEntry(arguments, "accessory", AccessoryNone);
		const DataModel::Accessory* accessory = manager.GetAccessory(accessoryID);
		if (accessory == nullptr)
		{
			ReplyResponse(ResponseError, Languages::TextAccessoryDoesNotExist);
			return;
		}

		string name = accessory->GetName();

		if (!manager.AccessoryDelete(accessoryID))
		{
			ReplyResponse(ResponseError, Languages::TextAccessoryDoesNotExist);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextAccessoryDeleted, name);
	}

	void WebClient::HandleAccessoryRelease(const map<string, string>& arguments)
	{
		accessoryID_t accessoryID = Utils::Utils::GetIntegerMapEntry(arguments, "accessory");
		bool ret = manager.AccessoryRelease(accessoryID);
		ReplyHtmlWithHeaderAndParagraph(ret ? "Accessory released" : "Accessory not released");
	}

	void WebClient::HandleSwitchEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		switchID_t switchID = Utils::Utils::GetIntegerMapEntry(arguments, "switch", SwitchNone);
		controlID_t controlID = ControlIdNone;
		protocol_t protocol = ProtocolNone;
		address_t address = AddressNone;
		string name = Languages::GetText(Languages::TextNew);
		layoutPosition_t posx = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posy = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posz = Utils::Utils::GetIntegerMapEntry(arguments, "posz", LayerUndeletable);
		DataModel::LayoutItem::layoutRotation_t rotation = static_cast<DataModel::LayoutItem::layoutRotation_t>(Utils::Utils::GetIntegerMapEntry(arguments, "rotation", DataModel::LayoutItem::Rotation0));
		switchType_t type = DataModel::Switch::SwitchTypeLeft;
		accessoryDuration_t duration = manager.GetDefaultAccessoryDuration();
		bool inverted = false;
		if (switchID > SwitchNone)
		{
			const DataModel::Switch* mySwitch = manager.GetSwitch(switchID);
			if (mySwitch != nullptr)
			{
				controlID = mySwitch->GetControlID();
				protocol = mySwitch->GetProtocol();
				address = mySwitch->GetAddress();
				name = mySwitch->GetName();
				posx = mySwitch->GetPosX();
				posy = mySwitch->GetPosY();
				posz = mySwitch->GetPosZ();
				rotation = mySwitch->GetRotation();
				type = mySwitch->GetType();
				duration = mySwitch->GetDuration();
				inverted = mySwitch->GetInverted();
			}
		}

		std::map<controlID_t,string> controls = manager.AccessoryControlListNames();
		std::map<string, string> controlOptions;
		for(auto control : controls)
		{
			controlOptions[to_string(control.first)] = control.second;
			if (controlID == ControlIdNone)
			{
				controlID = control.first;
			}
		}

		std::map<switchType_t,Languages::textSelector_t> typeOptions;
		typeOptions[DataModel::Switch::SwitchTypeLeft] = Languages::TextLeft;
		typeOptions[DataModel::Switch::SwitchTypeRight] = Languages::TextRight;

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddAttribute("id", "popup_title"));
		HtmlTag tabMenu("div");
		tabMenu.AddChildTag(HtmlTagTabMenuItem("main", Languages::TextBasic, true));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("position", Languages::TextPosition));
		content.AddChildTag(tabMenu);

		HtmlTag formContent;
		formContent.AddChildTag(HtmlTagInputHidden("cmd", "switchsave"));
		formContent.AddChildTag(HtmlTagInputHidden("switch", to_string(switchID)));

		HtmlTag mainContent("div");
		mainContent.AddAttribute("id", "tab_main");
		mainContent.AddClass("tab_content");
		mainContent.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));
		mainContent.AddChildTag(HtmlTagSelectWithLabel("type", Languages::TextType, typeOptions, type));
		mainContent.AddChildTag(HtmlTagSelectWithLabel("control", Languages::TextControl, controlOptions, to_string(controlID)).AddAttribute("onchange", "loadProtocol('switch', " + to_string(switchID) + ")"));
		mainContent.AddChildTag(HtmlTag("div").AddAttribute("id", "select_protocol").AddChildTag(HtmlTagProtocolAccessory(controlID, protocol)));
		mainContent.AddChildTag(HtmlTagInputIntegerWithLabel("address", Languages::TextAddress, address, 1, 2044));
		mainContent.AddChildTag(HtmlTagDuration(duration));
		mainContent.AddChildTag(HtmlTagInputCheckboxWithLabel("inverted", Languages::TextInverted, "true", inverted));
		formContent.AddChildTag(mainContent);

		HtmlTag positionContent("div");
		positionContent.AddAttribute("id", "tab_position");
		positionContent.AddClass("tab_content");
		positionContent.AddClass("hidden");
		positionContent.AddChildTag(HtmlTagPosition(posx, posy, posz));
		positionContent.AddChildTag(HtmlTagRotation(rotation));
		formContent.AddChildTag(positionContent);

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(HtmlTag("form").AddAttribute("id", "editform").AddChildTag(formContent)));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleSwitchSave(const map<string, string>& arguments)
	{
		switchID_t switchID = Utils::Utils::GetIntegerMapEntry(arguments, "switch", SwitchNone);
		string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		controlID_t controlId = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		protocol_t protocol = static_cast<protocol_t>(Utils::Utils::GetIntegerMapEntry(arguments, "protocol", ProtocolNone));
		address_t address = Utils::Utils::GetIntegerMapEntry(arguments, "address", AddressNone);
		layoutPosition_t posX = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posY = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posZ = Utils::Utils::GetIntegerMapEntry(arguments, "posz", 0);
		DataModel::LayoutItem::layoutRotation_t rotation = static_cast<DataModel::LayoutItem::layoutRotation_t>(Utils::Utils::GetIntegerMapEntry(arguments, "rotation", DataModel::LayoutItem::Rotation0));
		switchType_t type = Utils::Utils::GetIntegerMapEntry(arguments, "type", DataModel::Switch::SwitchTypeLeft);
		accessoryDuration_t duration = Utils::Utils::GetIntegerMapEntry(arguments, "duration", manager.GetDefaultAccessoryDuration());
		bool inverted = Utils::Utils::GetBoolMapEntry(arguments, "inverted");
		string result;
		if (!manager.SwitchSave(switchID, name, posX, posY, posZ, rotation, controlId, protocol, address, type, duration, inverted, result))
		{
			ReplyResponse(ResponseError, result);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextSwitchSaved, name);
	}

	void WebClient::HandleSwitchState(const map<string, string>& arguments)
	{
		switchID_t switchID = Utils::Utils::GetIntegerMapEntry(arguments, "switch", SwitchNone);
		switchState_t switchState = (Utils::Utils::GetStringMapEntry(arguments, "state", "turnout").compare("turnout") == 0 ? DataModel::Switch::SwitchStateTurnout : DataModel::Switch::SwitchStateStraight);

		manager.SwitchState(ControlTypeWebserver, switchID, switchState, false);

		ReplyHtmlWithHeaderAndParagraph(switchState ? Languages::TextSwitchStateIsStraight : Languages::TextSwitchStateIsTurnout, manager.GetSwitchName(switchID));
	}

	void WebClient::HandleSwitchList()
	{
		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextSwitches));
		HtmlTag table("table");
		const map<string,DataModel::Switch*> switchList = manager.SwitchListByName();
		map<string,string> switchArgument;
		for (auto mySwitch : switchList)
		{
			HtmlTag row("tr");
			row.AddChildTag(HtmlTag("td").AddContent(mySwitch.first));
			string switchIdString = to_string(mySwitch.second->GetID());
			switchArgument["switch"] = switchIdString;
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextEdit, "switchedit_list_" + switchIdString, switchArgument)));
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextDelete, "switchaskdelete_" + switchIdString, switchArgument)));
			if (mySwitch.second->IsInUse())
			{
				row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonCommandWide(Languages::TextRelease, "switchrelease_" + switchIdString, switchArgument, "hideElement('b_switchrelease_" + switchIdString + "');")));
			}
			table.AddChildTag(row);
		}
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(table));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonPopupWide(Languages::TextNew, "switchedit_0"));
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleSwitchAskDelete(const map<string, string>& arguments)
	{
		switchID_t switchID = Utils::Utils::GetIntegerMapEntry(arguments, "switch", SwitchNone);

		if (switchID == SwitchNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextSwitchDoesNotExist);
			return;
		}

		const DataModel::Switch* mySwitch = manager.GetSwitch(switchID);
		if (mySwitch == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextSwitchDoesNotExist);
			return;
		}

		HtmlTag content;
		const string& switchName = mySwitch->GetName();
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteSwitch));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, switchName));
		content.AddContent(HtmlTag("form").AddAttribute("id", "editform")
			.AddContent(HtmlTagInputHidden("cmd", "switchdelete"))
			.AddContent(HtmlTagInputHidden("switch", to_string(switchID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleSwitchDelete(const map<string, string>& arguments)
	{
		switchID_t switchID = Utils::Utils::GetIntegerMapEntry(arguments, "switch", SwitchNone);
		const DataModel::Switch* mySwitch = manager.GetSwitch(switchID);
		if (mySwitch == nullptr)
		{
			ReplyResponse(ResponseError, Languages::TextSwitchDoesNotExist);
			return;
		}

		string name = mySwitch->GetName();

		if (!manager.SwitchDelete(switchID))
		{
			ReplyResponse(ResponseError, Languages::TextSwitchDoesNotExist);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextSwitchDeleted, name);
	}

	void WebClient::HandleSwitchGet(const map<string, string>& arguments)
	{
		switchID_t switchID = Utils::Utils::GetIntegerMapEntry(arguments, "switch");
		const DataModel::Switch* mySwitch = manager.GetSwitch(switchID);
		if (mySwitch == nullptr)
		{
			ReplyHtmlWithHeader(HtmlTag());
			return;
		}
		ReplyHtmlWithHeader(HtmlTagSwitch(mySwitch));
	}

	void WebClient::HandleSwitchRelease(const map<string, string>& arguments)
	{
		switchID_t switchID = Utils::Utils::GetIntegerMapEntry(arguments, "switch");
		bool ret = manager.SwitchRelease(switchID);
		ReplyHtmlWithHeaderAndParagraph(ret ? "Switch released" : "Switch not released");
	}

	void WebClient::HandleSignalEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		signalID_t signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);
		controlID_t controlID = ControlIdNone;
		protocol_t protocol = ProtocolNone;
		address_t address = AddressNone;
		string name = Languages::GetText(Languages::TextNew);
		layoutPosition_t posx = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posy = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posz = Utils::Utils::GetIntegerMapEntry(arguments, "posz", LayerUndeletable);
		DataModel::LayoutItem::layoutRotation_t rotation = static_cast<DataModel::LayoutItem::layoutRotation_t>(Utils::Utils::GetIntegerMapEntry(arguments, "rotation", DataModel::LayoutItem::Rotation0));
		signalType_t type = DataModel::Signal::SignalTypeSimple;
		accessoryDuration_t duration = manager.GetDefaultAccessoryDuration();
		bool inverted = false;
		if (signalID > SignalNone)
		{
			const DataModel::Signal* signal = manager.GetSignal(signalID);
			if (signal != nullptr)
			{
				controlID = signal->GetControlID();
				protocol = signal->GetProtocol();
				address = signal->GetAddress();
				name = signal->GetName();
				posx = signal->GetPosX();
				posy = signal->GetPosY();
				posz = signal->GetPosZ();
				rotation = signal->GetRotation();
				type = signal->GetType();
				duration = signal->GetDuration();
				inverted = signal->GetInverted();
			}
		}

		std::map<controlID_t,string> controls = manager.AccessoryControlListNames();
		std::map<string, string> controlOptions;
		for(auto control : controls)
		{
			controlOptions[to_string(control.first)] = control.second;
			if (controlID == ControlIdNone)
			{
				controlID = control.first;
			}
		}

		std::map<signalType_t, Languages::textSelector_t> typeOptions;
		typeOptions[DataModel::Signal::SignalTypeSimple] = Languages::TextSimple;

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddAttribute("id", "popup_title"));
		HtmlTag tabMenu("div");
		tabMenu.AddChildTag(HtmlTagTabMenuItem("main", Languages::TextBasic, true));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("position", Languages::TextPosition));
		content.AddChildTag(tabMenu);

		HtmlTag formContent;
		formContent.AddChildTag(HtmlTagInputHidden("cmd", "signalsave"));
		formContent.AddChildTag(HtmlTagInputHidden("signal", to_string(signalID)));

		HtmlTag mainContent("div");
		mainContent.AddAttribute("id", "tab_main");
		mainContent.AddClass("tab_content");
		mainContent.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));
		mainContent.AddChildTag(HtmlTagSelectWithLabel("type", Languages::TextType, typeOptions, type));
		mainContent.AddChildTag(HtmlTagSelectWithLabel("control", Languages::TextControl, controlOptions, to_string(controlID)).AddAttribute("onchange", "loadProtocol('signal', " + to_string(signalID) + ")"));
		mainContent.AddChildTag(HtmlTag("div").AddAttribute("id", "select_protocol").AddChildTag(HtmlTagProtocolAccessory(controlID, protocol)));
		mainContent.AddChildTag(HtmlTagInputIntegerWithLabel("address", Languages::TextAddress, address, 1, 2044));
		mainContent.AddChildTag(HtmlTagDuration(duration));
		mainContent.AddChildTag(HtmlTagInputCheckboxWithLabel("inverted", Languages::TextInverted, "true", inverted));
		formContent.AddChildTag(mainContent);

		HtmlTag positionContent("div");
		positionContent.AddAttribute("id", "tab_position");
		positionContent.AddClass("tab_content");
		positionContent.AddClass("hidden");
		positionContent.AddChildTag(HtmlTagPosition(posx, posy, posz));
		positionContent.AddChildTag(HtmlTagRotation(rotation));
		formContent.AddChildTag(positionContent);

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(HtmlTag("form").AddAttribute("id", "editform").AddChildTag(formContent)));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleSignalSave(const map<string, string>& arguments)
	{
		signalID_t signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);
		string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		controlID_t controlId = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		protocol_t protocol = static_cast<protocol_t>(Utils::Utils::GetIntegerMapEntry(arguments, "protocol", ProtocolNone));
		address_t address = Utils::Utils::GetIntegerMapEntry(arguments, "address", AddressNone);
		layoutPosition_t posX = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posY = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posZ = Utils::Utils::GetIntegerMapEntry(arguments, "posz", 0);
		DataModel::LayoutItem::layoutRotation_t rotation = static_cast<DataModel::LayoutItem::layoutRotation_t>(Utils::Utils::GetIntegerMapEntry(arguments, "rotation", DataModel::LayoutItem::Rotation0));
		signalType_t type = Utils::Utils::GetIntegerMapEntry(arguments, "type", DataModel::Signal::SignalTypeSimple);
		accessoryDuration_t duration = Utils::Utils::GetIntegerMapEntry(arguments, "duration", manager.GetDefaultAccessoryDuration());
		bool inverted = Utils::Utils::GetBoolMapEntry(arguments, "inverted");
		string result;
		if (!manager.SignalSave(signalID, name, posX, posY, posZ, rotation, controlId, protocol, address, type, duration, inverted, result))
		{
			ReplyResponse(ResponseError, result);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextSignalSaved, name);
	}

	void WebClient::HandleSignalState(const map<string, string>& arguments)
	{
		signalID_t signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);
		signalState_t signalState = (Utils::Utils::GetStringMapEntry(arguments, "state", "red").compare("red") == 0 ? DataModel::Signal::SignalStateRed : DataModel::Signal::SignalStateGreen);

		manager.SignalState(ControlTypeWebserver, signalID, signalState, false);

		ReplyHtmlWithHeaderAndParagraph(signalState ? Languages::TextSignalStateIsGreen : Languages::TextSignalStateIsRed, manager.GetSignalName(signalID));
	}

	void WebClient::HandleSignalList()
	{
		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextSignals));
		HtmlTag table("table");
		const map<string,DataModel::Signal*> signalList = manager.SignalListByName();
		map<string,string> signalArgument;
		for (auto signal : signalList)
		{
			HtmlTag row("tr");
			row.AddChildTag(HtmlTag("td").AddContent(signal.first));
			string signalIdString = to_string(signal.second->GetID());
			signalArgument["signal"] = signalIdString;
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextEdit, "signaledit_list_" + signalIdString, signalArgument)));
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextDelete, "signalaskdelete_" + signalIdString, signalArgument)));
			if (signal.second->IsInUse())
			{
				row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonCommandWide(Languages::TextRelease, "signalrelease_" + signalIdString, signalArgument, "hideElement('b_signalrelease_" + signalIdString + "');")));
			}
			table.AddChildTag(row);
		}
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(table));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonPopupWide(Languages::TextNew, "signaledit_0"));
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleSignalAskDelete(const map<string, string>& arguments)
	{
		signalID_t signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);

		if (signalID == SignalNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextSignalDoesNotExist);
			return;
		}

		const DataModel::Signal* signal = manager.GetSignal(signalID);
		if (signal == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextSignalDoesNotExist);
			return;
		}

		HtmlTag content;
		const string& signalName = signal->GetName();
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteSignal));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, signalName));
		content.AddContent(HtmlTag("form").AddAttribute("id", "editform")
			.AddContent(HtmlTagInputHidden("cmd", "signaldelete"))
			.AddContent(HtmlTagInputHidden("signal", to_string(signalID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleSignalDelete(const map<string, string>& arguments)
	{
		signalID_t signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);
		const DataModel::Signal* signal = manager.GetSignal(signalID);
		if (signal == nullptr)
		{
			ReplyResponse(ResponseError, Languages::TextSignalDoesNotExist);
			return;
		}

		string name = signal->GetName();

		if (!manager.SignalDelete(signalID))
		{
			ReplyResponse(ResponseError, Languages::TextSignalDoesNotExist);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextSignalDeleted, name);
	}

	void WebClient::HandleSignalGet(const map<string, string>& arguments)
	{
		signalID_t signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal");
		const DataModel::Signal* signal = manager.GetSignal(signalID);
		if (signal == nullptr)
		{
			ReplyHtmlWithHeader(HtmlTag());
			return;
		}
		ReplyHtmlWithHeader(HtmlTagSignal(signal));
	}

	void WebClient::HandleSignalRelease(const map<string, string>& arguments)
	{
		signalID_t signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal");
		bool ret = manager.SignalRelease(signalID);
		ReplyHtmlWithHeaderAndParagraph(ret ? "Signal released" : "Signal not released");
	}

	void WebClient::HandleStreetGet(const map<string, string>& arguments)
	{
		streetID_t streetID = Utils::Utils::GetIntegerMapEntry(arguments, "street");
		const DataModel::Street* street = manager.GetStreet(streetID);
		if (street == nullptr || street->GetVisible() == VisibleNo)
		{
			ReplyHtmlWithHeader(HtmlTag());
			return;
		}
		ReplyHtmlWithHeader(HtmlTagStreet(street));
	}

	void WebClient::HandleStreetEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		streetID_t streetID = Utils::Utils::GetIntegerMapEntry(arguments, "street", StreetNone);
		string name = Languages::GetText(Languages::TextNew);
		delay_t delay = Street::DefaultDelay;
		Street::pushpullType_t pushpull = Street::PushpullTypeBoth;
		length_t minTrainLength = 0;
		length_t maxTrainLength = 0;
		vector<Relation*> relations;
		visible_t visible = static_cast<visible_t>(Utils::Utils::GetBoolMapEntry(arguments, "visible", VisibleYes));
		layoutPosition_t posx = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posy = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posz = Utils::Utils::GetIntegerMapEntry(arguments, "posz", LayerUndeletable);
		automode_t automode = static_cast<automode_t>(Utils::Utils::GetBoolMapEntry(arguments, "automode", AutomodeNo));
		trackID_t fromTrack = Utils::Utils::GetIntegerMapEntry(arguments, "fromtrack", TrackNone);
		direction_t fromDirection = static_cast<direction_t>(Utils::Utils::GetBoolMapEntry(arguments, "fromdirection", DirectionRight));
		trackID_t toTrack = Utils::Utils::GetIntegerMapEntry(arguments, "totrack", TrackNone);
		direction_t toDirection = static_cast<direction_t>(Utils::Utils::GetBoolMapEntry(arguments, "todirection", DirectionLeft));
		feedbackID_t feedbackIdReduced = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackreduced", FeedbackNone);
		feedbackID_t feedbackIdCreep = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackcreep", FeedbackNone);
		feedbackID_t feedbackIdStop = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackstop", FeedbackNone);
		feedbackID_t feedbackIdOver = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackover", FeedbackNone);
		wait_t waitAfterRelease = Utils::Utils::GetIntegerMapEntry(arguments, "waitafterrelease", 0);
		if (streetID > StreetNone)
		{
			const DataModel::Street* street = manager.GetStreet(streetID);
			if (street != nullptr)
			{
				name = street->GetName();
				delay = street->GetDelay();
				pushpull = street->GetPushpull();
				minTrainLength = street->GetMinTrainLength();
				maxTrainLength = street->GetMaxTrainLength();
				relations = street->GetRelations();
				visible = street->GetVisible();
				posx = street->GetPosX();
				posy = street->GetPosY();
				posz = street->GetPosZ();
				automode = street->GetAutomode();
				fromTrack = street->GetFromTrack();
				fromDirection = street->GetFromDirection();
				toTrack = street->GetToTrack();
				toDirection = street->GetToDirection();
				feedbackIdReduced = street->GetFeedbackIdReduced();
				feedbackIdCreep = street->GetFeedbackIdCreep();
				feedbackIdStop = street->GetFeedbackIdStop();
				feedbackIdOver = street->GetFeedbackIdOver();
				waitAfterRelease = street->GetWaitAfterRelease();
			}
		}

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddAttribute("id", "popup_title"));
		HtmlTag tabMenu("div");
		tabMenu.AddChildTag(HtmlTagTabMenuItem("basic", Languages::TextBasic, true));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("relation", Languages::TextMembers));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("position", Languages::TextPosition));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("automode", Languages::TextAutomode));
		content.AddChildTag(tabMenu);

		HtmlTag formContent("form");
		formContent.AddAttribute("id", "editform");
		formContent.AddChildTag(HtmlTagInputHidden("cmd", "streetsave"));
		formContent.AddChildTag(HtmlTagInputHidden("street", to_string(streetID)));

		HtmlTag basicContent("div");
		basicContent.AddAttribute("id", "tab_basic");
		basicContent.AddClass("tab_content");
		basicContent.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));
		basicContent.AddChildTag(HtmlTagInputIntegerWithLabel("delay", Languages::TextWaitingTimeBetweenMembers, delay, 1, USHRT_MAX));
		formContent.AddChildTag(basicContent);

		HtmlTag relationDiv("div");
		relationDiv.AddChildTag(HtmlTagInputHidden("relationcounter", to_string(relations.size())));
		relationDiv.AddAttribute("id", "relation");
		priority_t priority = 1;
		for (auto relation : relations)
		{
			relationDiv.AddChildTag(HtmlTagRelation(to_string(relation->Priority()), relation->ObjectType2(), relation->ObjectID2(), relation->AccessoryState()));
			priority = relation->Priority() + 1;
		}
		relationDiv.AddChildTag(HtmlTag("div").AddAttribute("id", "new_priority_" + to_string(priority)));
		HtmlTag relationContent("div");
		relationContent.AddAttribute("id", "tab_relation");
		relationContent.AddClass("tab_content");
		relationContent.AddClass("hidden");
		relationContent.AddChildTag(relationDiv);
		HtmlTagButton newButton(Languages::TextNew, "newrelation");
		newButton.AddAttribute("onclick", "addRelation();return false;");
		newButton.AddClass("wide_button");
		relationContent.AddChildTag(newButton);
		relationContent.AddChildTag(HtmlTag("br"));
		formContent.AddChildTag(relationContent);

		HtmlTag positionContent("div");
		positionContent.AddAttribute("id", "tab_position");
		positionContent.AddClass("tab_content");
		positionContent.AddClass("hidden");
		positionContent.AddChildTag(HtmlTagPosition(posx, posy, posz, visible));
		formContent.AddChildTag(positionContent);

		HtmlTag automodeContent("div");
		automodeContent.AddAttribute("id", "tab_automode");
		automodeContent.AddClass("tab_content");
		automodeContent.AddClass("hidden");

		HtmlTagInputCheckboxWithLabel checkboxAutomode("automode", Languages::TextAutomode, "automode", static_cast<bool>(automode));
		checkboxAutomode.AddAttribute("id", "automode");
		checkboxAutomode.AddAttribute("onchange", "onChangeCheckboxShowHide('automode', 'tracks');");
		automodeContent.AddChildTag(checkboxAutomode);

		HtmlTag tracksDiv("div");
		tracksDiv.AddAttribute("id", "tracks");
		if (automode == AutomodeNo)
		{
			tracksDiv.AddAttribute("hidden");
		}
		tracksDiv.AddChildTag(HtmlTagSelectTrack("from", Languages::TextFromTrack, fromTrack, fromDirection));
		tracksDiv.AddChildTag(HtmlTagSelectTrack("to", Languages::TextToTrack, toTrack, toDirection, "updateFeedbacksOfTrack(); return false;"));
		HtmlTag feedbackDiv("div");
		feedbackDiv.AddAttribute("id", "feedbacks");
		feedbackDiv.AddChildTag(HtmlTagSelectFeedbacksOfTrack(toTrack, feedbackIdReduced, feedbackIdCreep, feedbackIdStop, feedbackIdOver));
		tracksDiv.AddChildTag(feedbackDiv);
		map<Street::pushpullType_t,Languages::textSelector_t> pushpullOptions;
		pushpullOptions[Street::PushpullTypeNo] = Languages::TextNoPushPull;
		pushpullOptions[Street::PushpullTypeBoth] = Languages::TextAllTrains;
		pushpullOptions[Street::PushpullTypeOnly] = Languages::TextPushPullOnly;
		tracksDiv.AddChildTag(HtmlTagSelectWithLabel("pushpull", Languages::TextAllowedTrains, pushpullOptions, pushpull));
		tracksDiv.AddChildTag(HtmlTagInputIntegerWithLabel("mintrainlength", Languages::TextMinTrainLength, minTrainLength, 0, 99999));
		tracksDiv.AddChildTag(HtmlTagInputIntegerWithLabel("maxtrainlength", Languages::TextMaxTrainLength, maxTrainLength, 0, 99999));
		tracksDiv.AddChildTag(HtmlTagInputIntegerWithLabel("waitafterrelease", Languages::TextWaitAfterRelease, waitAfterRelease, 0, 300));
		automodeContent.AddChildTag(tracksDiv);
		formContent.AddChildTag(automodeContent);

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(formContent));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleFeedbacksOfTrack(const map<string, string>& arguments)
	{
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track", TrackNone);
		ReplyHtmlWithHeader(HtmlTagSelectFeedbacksOfTrack(trackID, FeedbackNone, FeedbackNone, FeedbackNone, FeedbackNone));
	}

	void WebClient::HandleStreetSave(const map<string, string>& arguments)
	{
		streetID_t streetID = Utils::Utils::GetIntegerMapEntry(arguments, "street", StreetNone);
		string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		delay_t delay = static_cast<delay_t>(Utils::Utils::GetIntegerMapEntry(arguments, "delay"));
		Street::pushpullType_t pushpull = static_cast<Street::pushpullType_t>(Utils::Utils::GetIntegerMapEntry(arguments, "pushpull", Street::PushpullTypeBoth));
		length_t mintrainlength = static_cast<length_t>(Utils::Utils::GetIntegerMapEntry(arguments, "mintrainlength", 0));
		length_t maxtrainlength = static_cast<length_t>(Utils::Utils::GetIntegerMapEntry(arguments, "maxtrainlength", 0));
		visible_t visible = static_cast<visible_t>(Utils::Utils::GetBoolMapEntry(arguments, "visible"));
		layoutPosition_t posx = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posy = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posz = Utils::Utils::GetIntegerMapEntry(arguments, "posz", 0);
		automode_t automode = static_cast<automode_t>(Utils::Utils::GetBoolMapEntry(arguments, "automode"));
		trackID_t fromTrack = Utils::Utils::GetIntegerMapEntry(arguments, "fromtrack", TrackNone);
		direction_t fromDirection = static_cast<direction_t>(Utils::Utils::GetBoolMapEntry(arguments, "fromdirection", DirectionRight));
		trackID_t toTrack = Utils::Utils::GetIntegerMapEntry(arguments, "totrack", TrackNone);
		direction_t toDirection = static_cast<direction_t>(Utils::Utils::GetBoolMapEntry(arguments, "todirection", DirectionLeft));
		feedbackID_t feedbackIdReduced = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackreduced", FeedbackNone);
		feedbackID_t feedbackIdCreep = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackcreep", FeedbackNone);
		feedbackID_t feedbackIdStop = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackstop", FeedbackNone);
		feedbackID_t feedbackIdOver = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackover", FeedbackNone);
		wait_t waitAfterRelease = Utils::Utils::GetIntegerMapEntry(arguments, "waitafterrelease", 0);

		vector<Relation*> relations;
		priority_t relationCount = Utils::Utils::GetIntegerMapEntry(arguments, "relationcounter", 0);
		priority_t priority = 1;
		for (priority_t relationId = 1; relationId <= relationCount; ++relationId)
		{
			string priorityString = to_string(relationId);
			objectType_t objectType = static_cast<objectType_t>(Utils::Utils::GetIntegerMapEntry(arguments, "relation_type_" + priorityString));
			objectID_t objectId = Utils::Utils::GetIntegerMapEntry(arguments, "relation_id_" + priorityString, SwitchNone);
			accessoryState_t state = Utils::Utils::GetIntegerMapEntry(arguments, "relation_state_" + priorityString);
			if (objectId == SwitchNone)
			{
				continue;
			}
			relations.push_back(new Relation(&manager, ObjectTypeStreet, streetID, objectType, objectId, priority, state));
			++priority;
		}

		string result;
		if (!manager.StreetSave(streetID,
			name,
			delay,
			pushpull,
			mintrainlength,
			maxtrainlength,
			relations,
			visible,
			posx,
			posy,
			posz,
			automode,
			fromTrack,
			fromDirection,
			toTrack,
			toDirection,
			feedbackIdReduced,
			feedbackIdCreep,
			feedbackIdStop,
			feedbackIdOver,
			waitAfterRelease,
			result))
		{
			ReplyResponse(ResponseError, result);
			return;
		}
		ReplyResponse(ResponseInfo, Languages::TextStreetSaved, name);
	}

	void WebClient::HandleStreetAskDelete(const map<string, string>& arguments)
	{
		streetID_t streetID = Utils::Utils::GetIntegerMapEntry(arguments, "street", StreetNone);

		if (streetID == StreetNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextStreetDoesNotExist);
			return;
		}

		const DataModel::Street* street = manager.GetStreet(streetID);
		if (street == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextStreetDoesNotExist);
			return;
		}

		HtmlTag content;
		const string& streetName = street->GetName();
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteStreet));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, streetName));
		content.AddContent(HtmlTag("form").AddAttribute("id", "editform")
			.AddContent(HtmlTagInputHidden("cmd", "streetdelete"))
			.AddContent(HtmlTagInputHidden("street", to_string(streetID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleStreetDelete(const map<string, string>& arguments)
	{
		streetID_t streetID = Utils::Utils::GetIntegerMapEntry(arguments, "street", StreetNone);
		const DataModel::Street* street = manager.GetStreet(streetID);
		if (street == nullptr)
		{
			ReplyResponse(ResponseError, Languages::TextStreetDoesNotExist);
			return;
		}

		string name = street->GetName();

		if (!manager.StreetDelete(streetID))
		{
			ReplyResponse(ResponseError, Languages::TextStreetDoesNotExist);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextStreetDeleted, name);
	}

	void WebClient::HandleStreetList()
	{
		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextStreets));
		HtmlTag table("table");
		const map<string,DataModel::Street*> streetList = manager.StreetListByName();
		map<string,string> streetArgument;
		for (auto street : streetList)
		{
			HtmlTag row("tr");
			row.AddChildTag(HtmlTag("td").AddContent(street.first));
			string streetIdString = to_string(street.second->GetID());
			streetArgument["street"] = streetIdString;
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextEdit, "streetedit_list_" + streetIdString, streetArgument)));
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextDelete, "streetaskdelete_" + streetIdString, streetArgument)));
			if (street.second->IsInUse())
			{
				row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonCommandWide(Languages::TextRelease, "streetrelease_" + streetIdString, streetArgument, "hideElement('b_streetrelease_" + streetIdString + "');")));
			}
			table.AddChildTag(row);
		}
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(table));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonPopupWide(Languages::TextNew, "streetedit_0"));
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleStreetExecute(const map<string, string>& arguments)
	{
		streetID_t streetID = Utils::Utils::GetIntegerMapEntry(arguments, "street", StreetNone);
		manager.StreetExecuteAsync(logger, streetID);
		ReplyHtmlWithHeaderAndParagraph("Street executed");
	}

	void WebClient::HandleStreetRelease(const map<string, string>& arguments)
	{
		streetID_t streetID = Utils::Utils::GetIntegerMapEntry(arguments, "street");
		bool ret = manager.StreetRelease(streetID);
		ReplyHtmlWithHeaderAndParagraph(ret ? "Street released" : "Street not released");
	}

	void WebClient::HandleTrackEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track", TrackNone);
		string name = Languages::GetText(Languages::TextNew);
		layoutPosition_t posx = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posy = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posz = Utils::Utils::GetIntegerMapEntry(arguments, "posz", 0);
		layoutItemSize_t height = Utils::Utils::GetIntegerMapEntry(arguments, "length", 1);
		DataModel::LayoutItem::layoutRotation_t rotation = static_cast<DataModel::LayoutItem::layoutRotation_t>(Utils::Utils::GetIntegerMapEntry(arguments, "rotation", DataModel::LayoutItem::Rotation0));
		DataModel::Track::type_t type = DataModel::Track::TrackTypeStraight;
		std::vector<feedbackID_t> feedbacks;
		DataModel::Track::selectStreetApproach_t selectStreetApproach = static_cast<DataModel::Track::selectStreetApproach_t>(Utils::Utils::GetIntegerMapEntry(arguments, "selectstreetapproach", DataModel::Track::SelectStreetSystemDefault));
		bool releaseWhenFree = Utils::Utils::GetBoolMapEntry(arguments, "releasewhenfree", false);
		if (trackID > TrackNone)
		{
			const DataModel::Track* track = manager.GetTrack(trackID);
			if (track != nullptr)
			{
				name = track->GetName();
				posx = track->GetPosX();
				posy = track->GetPosY();
				posz = track->GetPosZ();
				height = track->GetHeight();
				rotation = track->GetRotation();
				type = track->GetType();
				feedbacks = track->GetFeedbacks();
				selectStreetApproach = track->GetSelectStreetApproach();
				releaseWhenFree = track->GetReleaseWhenFree();
			}
		}
		switch (type)
		{
			case DataModel::Track::TrackTypeTurn:
			case DataModel::Track::TrackTypeTunnelEnd:
				height = 1;
				break;

			default:
				break;
		}

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddAttribute("id", "popup_title"));
		HtmlTag tabMenu("div");
		tabMenu.AddChildTag(HtmlTagTabMenuItem("main", Languages::TextBasic, true));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("position", Languages::TextPosition));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("feedback", Languages::TextFeedbacks));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("automode", Languages::TextAutomode));
		content.AddChildTag(tabMenu);

		HtmlTag formContent("form");
		formContent.AddAttribute("id", "editform");
		formContent.AddChildTag(HtmlTagInputHidden("cmd", "tracksave"));
		formContent.AddChildTag(HtmlTagInputHidden("track", to_string(trackID)));

		std::map<DataModel::Track::type_t, Languages::textSelector_t> typeOptions;
		typeOptions[DataModel::Track::TrackTypeStraight] = Languages::TextStraight;
		typeOptions[DataModel::Track::TrackTypeTurn] = Languages::TextTurn;
		typeOptions[DataModel::Track::TrackTypeEnd] = Languages::TextBufferStop;
		typeOptions[DataModel::Track::TrackTypeBridge] = Languages::TextBridge;
		typeOptions[DataModel::Track::TrackTypeTunnel] = Languages::TextTunnelTwoSides;
		typeOptions[DataModel::Track::TrackTypeTunnelEnd] = Languages::TextTunnelOneSide;
		typeOptions[DataModel::Track::TrackTypeLink] = Languages::TextLink;

		HtmlTag mainContent("div");
		mainContent.AddAttribute("id", "tab_main");
		mainContent.AddClass("tab_content");
		mainContent.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));
		mainContent.AddChildTag(HtmlTagSelectWithLabel("type", Languages::TextType, typeOptions, type).AddAttribute("onchange", "onChangeTrackType();return false;"));
		HtmlTag i_length("div");
		i_length.AddAttribute("id", "i_length");
		i_length.AddChildTag(HtmlTagInputIntegerWithLabel("length", Languages::TextLength, height, 1, 100));
		switch (type)
		{
			case DataModel::Track::TrackTypeTurn:
			case DataModel::Track::TrackTypeTunnelEnd:
				i_length.AddAttribute("hidden");
				break;

			default:
				break;
		}
		mainContent.AddChildTag(i_length);
		formContent.AddChildTag(mainContent);

		HtmlTag positionContent("div");
		positionContent.AddAttribute("id", "tab_position");
		positionContent.AddClass("tab_content");
		positionContent.AddClass("hidden");
		positionContent.AddChildTag(HtmlTagPosition(posx, posy, posz));
		positionContent.AddChildTag(HtmlTagRotation(rotation));
		formContent.AddChildTag(positionContent);

		HtmlTag automodeContent("div");
		automodeContent.AddAttribute("id", "tab_automode");
		automodeContent.AddClass("tab_content");
		automodeContent.AddClass("hidden");
		automodeContent.AddChildTag(HtmlTagSelectSelectStreetApproach(selectStreetApproach, true));
		automodeContent.AddChildTag(HtmlTagInputCheckboxWithLabel("releasewhenfree", Languages::TextReleaseWhenFree, "true", releaseWhenFree));
		formContent.AddChildTag(automodeContent);

		unsigned int feedbackCounter = 0;
		HtmlTag existingFeedbacks("div");
		existingFeedbacks.AddAttribute("id", "feedbackcontent");
		for (auto feedbackID : feedbacks)
		{
			existingFeedbacks.AddChildTag(HtmlTagSelectFeedbackForTrack(++feedbackCounter, trackID, feedbackID));
		}
		existingFeedbacks.AddChildTag(HtmlTag("div").AddAttribute("id", "div_feedback_" + to_string(feedbackCounter + 1)));

		HtmlTag feedbackContent("div");
		feedbackContent.AddAttribute("id", "tab_feedback");
		feedbackContent.AddClass("tab_content");
		feedbackContent.AddClass("hidden");
		feedbackContent.AddChildTag(HtmlTagInputHidden("feedbackcounter", to_string(feedbackCounter)));
		feedbackContent.AddChildTag(existingFeedbacks);
		HtmlTagButton newButton(Languages::TextNew, "newfeedback");
		newButton.AddAttribute("onclick", "addFeedback();return false;");
		newButton.AddClass("wide_button");
		feedbackContent.AddChildTag(newButton);
		feedbackContent.AddChildTag(HtmlTag("br"));
		formContent.AddChildTag(feedbackContent);

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(formContent));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleTrackSave(const map<string, string>& arguments)
	{
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track", TrackNone);
		string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		layoutPosition_t posX = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posY = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posZ = Utils::Utils::GetIntegerMapEntry(arguments, "posz", 0);
		layoutItemSize_t height = 1;
		DataModel::LayoutItem::layoutRotation_t rotation = static_cast<DataModel::LayoutItem::layoutRotation_t>(Utils::Utils::GetIntegerMapEntry(arguments, "rotation", DataModel::LayoutItem::Rotation0));
		DataModel::Track::type_t type = static_cast<DataModel::Track::type_t>(Utils::Utils::GetIntegerMapEntry(arguments, "type", DataModel::Track::TrackTypeStraight));
		switch (type)
		{
			case DataModel::Track::TrackTypeTurn:
			case DataModel::Track::TrackTypeTunnelEnd:
				break;

			default:
				height = Utils::Utils::GetIntegerMapEntry(arguments, "length", 1);
				break;
		}
		vector<feedbackID_t> feedbacks;
		unsigned int feedbackCounter = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackcounter", 1);
		for (unsigned int feedback = 1; feedback <= feedbackCounter; ++feedback)
		{
			feedbackID_t feedbackID = Utils::Utils::GetIntegerMapEntry(arguments, "feedback_" + to_string(feedback), FeedbackNone);
			if (feedbackID != FeedbackNone)
			{
				feedbacks.push_back(feedbackID);
			}
		}
		DataModel::Track::selectStreetApproach_t selectStreetApproach = static_cast<DataModel::Track::selectStreetApproach_t>(Utils::Utils::GetIntegerMapEntry(arguments, "selectstreetapproach", DataModel::Track::SelectStreetSystemDefault));
		bool releaseWhenFree = Utils::Utils::GetBoolMapEntry(arguments, "releasewhenfree", false);
		string result;
		if (manager.TrackSave(trackID, name, posX, posY, posZ, height, rotation, type, feedbacks, selectStreetApproach, releaseWhenFree, result) == TrackNone)
		{
			ReplyResponse(ResponseError, result);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextTrackSaved, name);
	}

	void WebClient::HandleTrackAskDelete(const map<string, string>& arguments)
	{
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track", TrackNone);

		if (trackID == TrackNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextTrackDoesNotExist);
			return;
		}

		const DataModel::Track* track = manager.GetTrack(trackID);
		if (track == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextTrackDoesNotExist);
			return;
		}

		HtmlTag content;
		const string& trackName = track->GetName();
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteTrack));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, trackName));
		content.AddContent(HtmlTag("form").AddAttribute("id", "editform")
			.AddContent(HtmlTagInputHidden("cmd", "trackdelete"))
			.AddContent(HtmlTagInputHidden("track", to_string(trackID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleTrackList()
	{
		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextTracks));
		HtmlTag table("table");
		const map<string,DataModel::Track*> trackList = manager.TrackListByName();
		map<string,string> trackArgument;
		for (auto track : trackList)
		{
			HtmlTag row("tr");
			row.AddChildTag(HtmlTag("td").AddContent(track.first));
			string trackIdString = to_string(track.second->GetID());
			trackArgument["track"] = trackIdString;
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextEdit, "trackedit_list_" + trackIdString, trackArgument)));
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextDelete, "trackaskdelete_" + trackIdString, trackArgument)));
			if (track.second->IsInUse())
			{
				row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonCommandWide(Languages::TextRelease, "trackrelease_" + trackIdString, trackArgument, "hideElement('b_trackrelease_" + trackIdString + "');")));
			}
			table.AddChildTag(row);
		}
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(table));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonPopupWide(Languages::TextNew, "trackedit_0"));
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleTrackDelete(const map<string, string>& arguments)
	{
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track", TrackNone);
		const DataModel::Track* track = manager.GetTrack(trackID);
		if (track == nullptr)
		{
			ReplyResponse(ResponseError, Languages::TextTrackDoesNotExist);
			return;
		}

		string name = track->GetName();

		if (!manager.TrackDelete(trackID))
		{
			ReplyResponse(ResponseError, Languages::TextTrackDoesNotExist);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextTrackDeleted, name);
	}

	void WebClient::HandleTrackGet(const map<string, string>& arguments)
	{
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track");
		const DataModel::Track* track = manager.GetTrack(trackID);
		if (track == nullptr)
		{
			ReplyHtmlWithHeader(HtmlTag());
			return;
		}
		ReplyHtmlWithHeader(HtmlTagTrack(manager, track));
	}

	void WebClient::HandleTrackSetLoco(const map<string, string>& arguments)
	{
		HtmlTag content;
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track", TrackNone);
		locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		if (locoID != LocoNone)
		{
			if (!manager.LocoIntoTrack(locoID, trackID))
			{
				ReplyResponse(ResponseError, Languages::TextUnableToAddLocoToTrack, manager.GetLocoName(locoID), manager.GetTrackName(trackID));
				return;
			}
			ReplyResponse(ResponseInfo, Languages::TextLocoIsOnTrack, manager.GetLocoName(locoID), manager.GetTrackName(trackID));
			return;
		}
		const DataModel::Track* track = manager.GetTrack(trackID);
		if (track->IsInUse())
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextTrackIsInUse, track->GetName());
			return;
		}
		map<string,locoID_t> locos = manager.LocoListFree();
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextSelectLocoForTrack, track->GetName()));
		content.AddChildTag(HtmlTagInputHidden("cmd", "tracksetloco"));
		content.AddChildTag(HtmlTagInputHidden("track", to_string(trackID)));
		content.AddChildTag(HtmlTagSelectWithLabel("loco", Languages::TextLoco, locos));
		content.AddChildTag(HtmlTag("br"));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(HtmlTag("form").AddAttribute("id", "editform").AddChildTag(content));
	}

	void WebClient::HandleTrackRelease(const map<string, string>& arguments)
	{
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track");
		bool ret = manager.TrackRelease(trackID);
		ReplyHtmlWithHeaderAndParagraph(ret ? "Track released" : "Track not released");
	}

	void WebClient::HandleTrackStartLoco(const map<string, string>& arguments)
	{
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track");
		bool ret = manager.TrackStartLoco(trackID);
		ReplyHtmlWithHeaderAndParagraph(ret ? "Loco started" : "Loco not started");
	}

	void WebClient::HandleTrackStopLoco(const map<string, string>& arguments)
	{
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track");
		bool ret = manager.TrackStopLoco(trackID);
		ReplyHtmlWithHeaderAndParagraph(ret ? "Loco stopped" : "Loco not stopped");
	}

	void WebClient::HandleTrackBlock(const map<string, string>& arguments)
	{
		trackID_t trackID = Utils::Utils::GetIntegerMapEntry(arguments, "track");
		bool blocked = Utils::Utils::GetBoolMapEntry(arguments, "blocked");
		manager.TrackBlock(trackID, blocked);
		ReplyHtmlWithHeaderAndParagraph("Track block/unblock received");
	}

	void WebClient::HandleFeedbackEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		feedbackID_t feedbackID = Utils::Utils::GetIntegerMapEntry(arguments, "feedback", FeedbackNone);
		string name = Languages::GetText(Languages::TextNew);
		controlID_t controlId = Utils::Utils::GetIntegerMapEntry(arguments, "controlid", ControlNone);
		feedbackPin_t pin = Utils::Utils::GetIntegerMapEntry(arguments, "pin", 0);
		layoutPosition_t posx = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posy = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posz = Utils::Utils::GetIntegerMapEntry(arguments, "posz", LayerUndeletable);
		visible_t visible = static_cast<visible_t>(Utils::Utils::GetBoolMapEntry(arguments, "visible", feedbackID == FeedbackNone && ((posx || posy) && posz >= LayerUndeletable) ? VisibleYes : VisibleNo));
		if (posz < LayerUndeletable)
		{
			if (controlId == ControlNone)
			{
				controlId = -posz;
			}
			if (pin == 0)
			{
				pin = posy * 16 + posx + (posx > 8 ? 0 : 1);
			}
		}
		bool inverted = false;
		if (feedbackID > FeedbackNone)
		{
			const DataModel::Feedback* feedback = manager.GetFeedback(feedbackID);
			if (feedback != nullptr)
			{
				name = feedback->GetName();
				controlId = feedback->GetControlID();
				pin = feedback->GetPin();
				inverted = feedback->GetInverted();
				visible = feedback->GetVisible();
				posx = feedback->GetPosX();
				posy = feedback->GetPosY();
				posz = feedback->GetPosZ();
			}
		}

		std::map<controlID_t,string> controls = manager.FeedbackControlListNames();
		std::map<string, string> controlOptions;
		for(auto control : controls)
		{
			controlOptions[to_string(control.first)] = control.second;
			if (controlId == ControlIdNone)
			{
				controlId = control.first;
			}
		}

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddAttribute("id", "popup_title"));

		HtmlTag tabMenu("div");
		tabMenu.AddChildTag(HtmlTagTabMenuItem("main", Languages::TextBasic, true));
		tabMenu.AddChildTag(HtmlTagTabMenuItem("position", Languages::TextPosition));
		content.AddChildTag(tabMenu);

		HtmlTag formContent("form");
		formContent.AddAttribute("id", "editform");
		formContent.AddChildTag(HtmlTagInputHidden("cmd", "feedbacksave"));
		formContent.AddChildTag(HtmlTagInputHidden("feedback", to_string(feedbackID)));

		HtmlTag mainContent("div");
		mainContent.AddAttribute("id", "tab_main");
		mainContent.AddClass("tab_content");
		mainContent.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));
		mainContent.AddChildTag(HtmlTagSelectWithLabel("control", Languages::TextControl, controlOptions, to_string(controlId)).AddAttribute("onchange", "loadProtocol('feedback', " + to_string(feedbackID) + ")"));
		mainContent.AddChildTag(HtmlTagInputIntegerWithLabel("pin", Languages::TextPin, pin, 1, 4096));
		mainContent.AddChildTag(HtmlTagInputCheckboxWithLabel("inverted", Languages::TextInverted, "true", inverted));
		formContent.AddChildTag(mainContent);

		HtmlTag positionContent("div");
		positionContent.AddAttribute("id", "tab_position");
		positionContent.AddClass("tab_content");
		positionContent.AddClass("hidden");
		positionContent.AddChildTag(HtmlTagPosition(posx, posy, posz, visible));
		formContent.AddChildTag(positionContent);

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(formContent));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleFeedbackSave(const map<string, string>& arguments)
	{
		feedbackID_t feedbackID = Utils::Utils::GetIntegerMapEntry(arguments, "feedback", FeedbackNone);
		string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		controlID_t controlId = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		feedbackPin_t pin = static_cast<feedbackPin_t>(Utils::Utils::GetIntegerMapEntry(arguments, "pin", FeedbackPinNone));
		bool inverted = Utils::Utils::GetBoolMapEntry(arguments, "inverted");
		visible_t visible = static_cast<visible_t>(Utils::Utils::GetBoolMapEntry(arguments, "visible", VisibleNo));
		layoutPosition_t posX = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		layoutPosition_t posY = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		layoutPosition_t posZ = Utils::Utils::GetIntegerMapEntry(arguments, "posz", 0);
		string result;
		if (manager.FeedbackSave(feedbackID, name, visible, posX, posY, posZ, controlId, pin, inverted, result) == FeedbackNone)
		{
			ReplyResponse(ResponseError, result);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextFeedbackSaved, name);
	}

	void WebClient::HandleFeedbackState(const map<string, string>& arguments)
	{
		feedbackID_t feedbackID = Utils::Utils::GetIntegerMapEntry(arguments, "feedback", FeedbackNone);
		DataModel::Feedback::feedbackState_t state = (Utils::Utils::GetStringMapEntry(arguments, "state", "occupied").compare("occupied") == 0 ? DataModel::Feedback::FeedbackStateOccupied : DataModel::Feedback::FeedbackStateFree);

		manager.FeedbackState(feedbackID, state);

		ReplyHtmlWithHeaderAndParagraph(state ? Languages::TextFeedbackStateIsOn : Languages::TextFeedbackStateIsOff, manager.GetFeedbackName(feedbackID));
	}

	void WebClient::HandleFeedbackList()
	{
		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextFeedbacks));
		HtmlTag table("table");
		const map<string,DataModel::Feedback*> feedbackList = manager.FeedbackListByName();
		map<string,string> feedbackArgument;
		for (auto feedback : feedbackList)
		{
			HtmlTag row("tr");
			row.AddChildTag(HtmlTag("td").AddContent(feedback.first));
			string feedbackIdString = to_string(feedback.second->GetID());
			feedbackArgument["feedback"] = feedbackIdString;
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextEdit, "feedbackedit_list_" + feedbackIdString, feedbackArgument)));
			row.AddChildTag(HtmlTag("td").AddChildTag(HtmlTagButtonPopupWide(Languages::TextDelete, "feedbackaskdelete_" + feedbackIdString, feedbackArgument)));
			table.AddChildTag(row);
		}
		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(table));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonPopupWide(Languages::TextNew, "feedbackedit_0"));
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleFeedbackAskDelete(const map<string, string>& arguments)
	{
		feedbackID_t feedbackID = Utils::Utils::GetIntegerMapEntry(arguments, "feedback", FeedbackNone);

		if (feedbackID == FeedbackNone)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextFeedbackDoesNotExist);
			return;
		}

		const DataModel::Feedback* feedback = manager.GetFeedback(feedbackID);
		if (feedback == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextFeedbackDoesNotExist);
			return;
		}

		HtmlTag content;
		const string& feedbackName = feedback->GetName();
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteFeedback));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, feedbackName));
		content.AddContent(HtmlTag("form").AddAttribute("id", "editform")
			.AddContent(HtmlTagInputHidden("cmd", "feedbackdelete"))
			.AddContent(HtmlTagInputHidden("feedback", to_string(feedbackID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleFeedbackDelete(const map<string, string>& arguments)
	{
		feedbackID_t feedbackID = Utils::Utils::GetIntegerMapEntry(arguments, "feedback", FeedbackNone);
		const DataModel::Feedback* feedback = manager.GetFeedback(feedbackID);
		if (feedback == nullptr)
		{
			ReplyResponse(ResponseError, Languages::TextFeedbackDoesNotExist);
			return;
		}

		string name = feedback->GetName();

		if (!manager.FeedbackDelete(feedbackID))
		{
			ReplyResponse(ResponseError, Languages::TextFeedbackDoesNotExist);
			return;
		}

		ReplyResponse(ResponseInfo, Languages::TextFeedbackDeleted, name);
	}

	HtmlTag WebClient::HtmlTagFeedbackOnControlLayer(const Feedback* feedback)
	{
		feedbackPin_t pin = feedback->GetPin() - 1;
		layoutPosition_t x = pin & 0x0F; // => % 16;
		layoutPosition_t y = pin >> 4;   // => / 16;
		x += x >> 3; // => if (x >= 8) ++x;
		return HtmlTagFeedback(feedback, x, y);
	}

	void WebClient::HandleFeedbackGet(const map<string, string>& arguments)
	{
		feedbackID_t feedbackID = Utils::Utils::GetIntegerMapEntry(arguments, "feedback", FeedbackNone);
		const DataModel::Feedback* feedback = manager.GetFeedback(feedbackID);
		if (feedback == nullptr)
		{
			ReplyHtmlWithHeader(HtmlTag());
			return;
		}

		layerID_t layer = Utils::Utils::GetIntegerMapEntry(arguments, "layer", LayerNone);
		if (feedback->GetControlID() == -layer)
		{
			ReplyHtmlWithHeader(HtmlTagFeedbackOnControlLayer(feedback));
			return;
		}

		if (layer < LayerNone || feedback->GetVisible() == VisibleNo)
		{
			ReplyHtmlWithHeader(HtmlTag());
			return;
		}

		ReplyHtmlWithHeader(HtmlTagFeedback(feedback));
	}

	void WebClient::HandleLocoSelector()
	{
		ReplyHtmlWithHeader(HtmlTagLocoSelector());
	}

	void WebClient::HandleLayerSelector()
	{
		ReplyHtmlWithHeader(HtmlTagLayerSelector());
	}

	void WebClient::HandleSettingsEdit()
	{
		const accessoryDuration_t defaultAccessoryDuration = manager.GetDefaultAccessoryDuration();
		const bool autoAddFeedback = manager.GetAutoAddFeedback();
		const DataModel::Track::selectStreetApproach_t selectStreetApproach = manager.GetSelectStreetApproach();
		const DataModel::Loco::nrOfTracksToReserve_t nrOfTracksToReserve = manager.GetNrOfTracksToReserve();

		HtmlTag content;
		content.AddChildTag(HtmlTag("h1").AddContent(Languages::TextSettings));

		HtmlTag formContent("form");
		formContent.AddAttribute("id", "editform");
		formContent.AddChildTag(HtmlTagInputHidden("cmd", "settingssave"));
		formContent.AddChildTag(HtmlTagDuration(defaultAccessoryDuration, Languages::TextDefaultSwitchingDuration));
		formContent.AddChildTag(HtmlTagInputCheckboxWithLabel("autoaddfeedback", Languages::TextAutomaticallyAddUnknownFeedbacks, "autoaddfeedback", autoAddFeedback));
		formContent.AddChildTag(HtmlTagSelectSelectStreetApproach(selectStreetApproach, false));
		formContent.AddChildTag(HtmlTagNrOfTracksToReserve(nrOfTracksToReserve));

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(formContent));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		ReplyHtmlWithHeader(content);
	}

	void WebClient::HandleSettingsSave(const map<string, string>& arguments)
	{
		const accessoryDuration_t defaultAccessoryDuration = Utils::Utils::GetIntegerMapEntry(arguments, "duration", manager.GetDefaultAccessoryDuration());
		const bool autoAddFeedback = Utils::Utils::GetBoolMapEntry(arguments, "autoaddfeedback", manager.GetAutoAddFeedback());
		const DataModel::Track::selectStreetApproach_t selectStreetApproach = static_cast<DataModel::Track::selectStreetApproach_t>(Utils::Utils::GetIntegerMapEntry(arguments, "selectstreetapproach", DataModel::Track::SelectStreetRandom));
		const DataModel::Loco::nrOfTracksToReserve_t nrOfTracksToReserve = static_cast<DataModel::Loco::nrOfTracksToReserve_t>(Utils::Utils::GetIntegerMapEntry(arguments, "nroftrackstoreserve", DataModel::Loco::ReserveOne));
		manager.SaveSettings(defaultAccessoryDuration, autoAddFeedback, selectStreetApproach, nrOfTracksToReserve);
		ReplyResponse(ResponseInfo, Languages::TextSettingsSaved);
	}

	void WebClient::HandleTimestamp(__attribute__((unused)) const map<string, string>& arguments)
	{
#ifdef __CYGWIN__
		ReplyHtmlWithHeaderAndParagraph(Languages::TextTimestampNotSet);
#else
		const time_t timestamp = Utils::Utils::GetIntegerMapEntry(arguments, "timestamp", 0);
		if (timestamp == 0)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextTimestampNotSet);
			return;
		}
		struct timeval tv;
		int ret = gettimeofday(&tv, nullptr);
		if (ret != 0 || tv.tv_sec > GetCompileTime())
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextTimestampAlreadySet);
			return;
		}

		tv.tv_sec = timestamp;
		ret = settimeofday(&tv, nullptr);
		if (ret != 0)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextTimestampNotSet);
			return;
		}
		ReplyHtmlWithHeaderAndParagraph(Languages::TextTimestampSet);
#endif
	}

	void WebClient::HandleControlArguments(const map<string, string>& arguments)
	{
		hardwareType_t hardwareType = static_cast<hardwareType_t>(Utils::Utils::GetIntegerMapEntry(arguments, "hardwaretype"));
		ReplyHtmlWithHeader(HtmlTagControlArguments(hardwareType));
	}

	void WebClient::HandleUpdater(const map<string, string>& headers)
	{
		Response response;
		response.AddHeader("Cache-Control", "no-cache, must-revalidate");
		response.AddHeader("Pragma", "no-cache");
		response.AddHeader("Expires", "Sun, 12 Feb 2016 00:00:00 GMT");
		response.AddHeader("Content-Type", "text/event-stream; charset=utf-8");
		int ret = connection->Send(response);
		if (ret <= 0)
		{
			return;
		}

		unsigned int updateID = Utils::Utils::GetIntegerMapEntry(headers, "Last-Event-ID", 1);
		while(run)
		{
			string s;
			bool ok = server.NextUpdate(updateID, s);
			if (ok == false)
			{
				// FIXME: use signaling instead of sleep
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			string reply("id: ");
			reply += to_string(updateID);
			reply += "\r\n";
			reply += s;
			reply += "\r\n\r\n";

			++updateID;

			ret = connection->Send(reply);
			if (ret < 0)
			{
				return;
			}
		}
	}

	void WebClient::ReplyHtmlWithHeader(const HtmlTag& tag)
	{
		connection->Send(HtmlResponse(tag));
	}

	HtmlTag WebClient::HtmlTagLocoSelector() const
	{
		const map<locoID_t, Loco*>& locos = manager.locoList();
		map<string,locoID_t> options;
		for (auto locoTMP : locos)
		{
			Loco* loco = locoTMP.second;
			options[loco->GetName()] = loco->GetID();
		}
		return HtmlTagSelect("loco", options).AddAttribute("onchange", "loadLoco();");
	}

	void WebClient::PrintLoco(const map<string, string>& arguments)
	{
		string content;
		locoID_t locoID = Utils::Utils::GetIntegerMapEntry(arguments, "loco", LocoNone);
		Loco* loco = manager.GetLoco(locoID);
		if (loco == nullptr)
		{
			ReplyHtmlWithHeaderAndParagraph(Languages::TextLocoDoesNotExist);
			return;
		}

		stringstream ss;
		ss << HtmlTag("p").AddContent(loco->GetName());
		unsigned int speed = loco->Speed();
		map<string, string> buttonArguments;
		buttonArguments["loco"] = to_string(locoID);

		string id = "locospeed_" + to_string(locoID);
		ss << HtmlTagInputSliderLocoSpeed("speed", MinSpeed, loco->GetMaxSpeed(), speed, locoID);
		buttonArguments["speed"] = to_string(MinSpeed);
		ss << HtmlTagButtonCommand("0", id + "_0", buttonArguments);
		buttonArguments["speed"] = to_string(loco->GetCreepingSpeed());
		ss << HtmlTagButtonCommand("I", id + "_1", buttonArguments);
		buttonArguments["speed"] = to_string(loco->GetReducedSpeed());
		ss << HtmlTagButtonCommand("II", id + "_2", buttonArguments);
		buttonArguments["speed"] = to_string(loco->GetTravelSpeed());
		ss << HtmlTagButtonCommand("III", id + "_3", buttonArguments);
		buttonArguments["speed"] = to_string(loco->GetMaxSpeed());
		ss << HtmlTagButtonCommand("IV", id + "_4", buttonArguments);
		buttonArguments.erase("speed");

		id = "locoedit_" + to_string(locoID);
		ss << HtmlTagButtonPopup(HtmlTag("span").AddClass("symbola").AddContent("&#x270D;"), id, buttonArguments);

		id = "locodirection_" + to_string(locoID);
		ss << HtmlTagButtonCommandToggle(HtmlTag("span").AddClass("symbola").AddContent("&#9193;"), id, loco->GetDirection(), buttonArguments).AddClass("button_direction");

		id = "locofunction_" + to_string(locoID);
		function_t nrOfFunctions = loco->GetNrOfFunctions();
		for (function_t nr = 0; nr <= nrOfFunctions; ++nr)
		{
			string nrText(to_string(nr));
			buttonArguments["function"] = nrText;
			ss << HtmlTagButtonCommandToggle("f" + nrText, id + "_" + nrText, loco->GetFunction(nr), buttonArguments);
		}
		buttonArguments.erase("function");
		ReplyHtmlWithHeaderAndParagraph(ss.str());
	}

	void WebClient::PrintMainHTML() {
		// handle base request
		HtmlTag body("body");
		body.AddAttribute("onload", "startUp();");
		body.AddAttribute("id", "body");

		map<string,string> buttonArguments;

		HtmlTag menu("div");
		menu.AddClass("menu");
		HtmlTag menuMain("div");
		menuMain.AddClass("menu_main");
		menuMain.AddChildTag(HtmlTagButtonCommand("<svg width=\"36\" height=\"36\"><polygon points=\"16,1.5 31,1.5 31,25.5 16,25.5\" fill=\"white\" style=\"stroke:black;stroke-width:1;\"/><polygon points=\"21,11.5 31,1.5 31,25.5 21,35.5\" fill=\"black\" style=\"stroke:black;stroke-width:1;\"/><polygon points=\"1,11 8.5,11 8.5,6 16,13.5 8.5,21 8.5,16 1,16\"/></svg>", "quit"));
		menuMain.AddChildTag(HtmlTagButtonCommandToggle("<svg width=\"36\" height=\"36\"><polyline points=\"13.5,9.8 12.1,10.8 10.8,12.1 9.8,13.5 9.1,15.1 8.7,16.8 8.5,18.5 8.7,20.2 9.1,21.9 9.8,23.5 10.8,24.9 12.1,26.2 13.5,27.2 15.1,27.9 16.8,28.3 18.5,28.5 20.2,28.3 21.9,27.9 23.5,27.2 24.9,26.2 26.2,24.9 27.2,23.5 27.9,21.9 28.3,20.2 28.5,18.5 28.3,16.8 27.9,15.1 27.2,13.5 26.2,12.1 24.9,10.8 23.5,9.8\" stroke=\"black\" stroke-width=\"3\" fill=\"none\"/><polyline points=\"18.5,3.5 18.5,16\" stroke=\"black\" stroke-width=\"3\" fill=\"none\"/></svg>", "booster", manager.Booster(), buttonArguments).AddClass("button_booster"));
		menuMain.AddChildTag(HtmlTagButtonCommand("<svg width=\"36\" height=\"36\"><polyline points=\"2,12 2,11 11,2 26,2 35,11 35,26 26,35 11,35 2,26 2,12\" stroke=\"black\" stroke-width=\"1\" fill=\"red\"/><text x=\"4\" y=\"22\" fill=\"white\" font-size=\"11\">STOP</text></svg>", "stopallimmediately"));
		menuMain.AddChildTag(HtmlTagButtonCommand("<svg width=\"36\" height=\"36\"><polygon points=\"17,36 17,28 15,28 10,23 10,5 15,0 21,0 26,5 26,23 21,28 19,28 19,36\" fill=\"black\" /><circle cx=\"18\" cy=\"8\" r=\"4\" fill=\"red\" /><circle cx=\"18\" cy=\"20\" r=\"4\" fill=\"darkgray\" /></svg>", "stopall"));
		menuMain.AddChildTag(HtmlTagButtonCommand("<svg width=\"36\" height=\"36\"><polygon points=\"17,36 17,28 15,28 10,23 10,5 15,0 21,0 26,5 26,23 21,28 19,28 19,36\" fill=\"black\" /><circle cx=\"18\" cy=\"8\" r=\"4\" fill=\"darkgray\" /><circle cx=\"18\" cy=\"20\" r=\"4\" fill=\"green\" /></svg>", "startall"));
		menu.AddChildTag(menuMain);

		HtmlTag menuAdd("div");
		menuAdd.AddClass("menu_add");
		menuAdd.AddChildTag(HtmlTag().AddContent("&nbsp;&nbsp;&nbsp;"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><circle r=\"7\" cx=\"14\" cy=\"14\" fill=\"black\" /><line x1=\"14\" y1=\"5\" x2=\"14\" y2=\"23\" stroke-width=\"2\" stroke=\"black\" /><line x1=\"9.5\" y1=\"6.2\" x2=\"18.5\" y2=\"21.8\" stroke-width=\"2\" stroke=\"black\" /><line x1=\"6.2\" y1=\"9.5\" x2=\"21.8\" y2=\"18.5\" stroke-width=\"2\" stroke=\"black\" /><line y1=\"14\" x1=\"5\" y2=\"14\" x2=\"23\" stroke-width=\"2\" stroke=\"black\" /><line x1=\"9.5\" y1=\"21.8\" x2=\"18.5\" y2=\"6.2\" stroke-width=\"2\" stroke=\"black\" /><line x1=\"6.2\" y1=\"18.5\" x2=\"21.8\" y2=\"9.5\" stroke-width=\"2\" stroke=\"black\" /><circle r=\"5\" cx=\"14\" cy=\"14\" fill=\"white\" /><circle r=\"4\" cx=\"24\" cy=\"24\" fill=\"black\" /><line x1=\"18\" y1=\"24\" x2=\"30\" y2=\"24\" stroke-width=\"2\" stroke=\"black\" /><line x1=\"28.2\" y1=\"28.2\" x2=\"19.8\" y2=\"19.8\" stroke-width=\"2\" stroke=\"black\" /><line x1=\"24\" y1=\"18\" x2=\"24\" y2=\"30\" stroke-width=\"2\" stroke=\"black\" /><line x1=\"19.8\" y1=\"28.2\" x2=\"28.2\" y2=\"19.8\" stroke-width=\"2\" stroke=\"black\" /><circle r=\"2\" cx=\"24\" cy=\"24\" fill=\"white\" /></svg>", "settingsedit"));
		menuAdd.AddChildTag(HtmlTag().AddContent("&nbsp;&nbsp;&nbsp;"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><polygon points=\"11,1.5 26,1.5 26,35.5 11,35.5\" fill=\"white\" style=\"stroke:black;stroke-width:1;\"/><polygon points=\"14,4.5 23,4.5 23,8.5 14,8.5\" fill=\"white\" style=\"stroke:black;stroke-width:1;\"/><circle cx=\"15.5\" cy=\"12\" r=\"1\" fill=\"black\"/><circle cx=\"18.5\" cy=\"12\" r=\"1\" fill=\"black\"/><circle cx=\"21.5\" cy=\"12\" r=\"1\" fill=\"black\"/><circle cx=\"15.5\" cy=\"15\" r=\"1\" fill=\"black\"/><circle cx=\"18.5\" cy=\"15\" r=\"1\" fill=\"black\"/><circle cx=\"21.5\" cy=\"15\" r=\"1\" fill=\"black\"/><circle cx=\"15.5\" cy=\"18\" r=\"1\" fill=\"black\"/><circle cx=\"18.5\" cy=\"18\" r=\"1\" fill=\"black\"/><circle cx=\"21.5\" cy=\"18\" r=\"1\" fill=\"black\"/><circle cx=\"15.5\" cy=\"21\" r=\"1\" fill=\"black\"/><circle cx=\"18.5\" cy=\"21\" r=\"1\" fill=\"black\"/><circle cx=\"21.5\" cy=\"21\" r=\"1\" fill=\"black\"/><circle cx=\"18.5\" cy=\"28.5\" r=\"5\" fill=\"black\"/></svg>", "controllist"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><polygon points=\"1,11 6,11 6,1 11,1 11,11 26,11 26,1 36,1 36,6 31,6 31,11 36,11 36,26 1,26\" fill=\"black\"/><circle cx=\"6\" cy=\"31\" r=\"5\" fill=\"black\"/><circle cx=\"18.5\" cy=\"31\" r=\"5\" fill=\"black\"/><circle cx=\"31\" cy=\"31\" r=\"5\" fill=\"black\"/</svg>", "locolist"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><polygon points=\"2,31 26,31 35,21 11,21\" fill=\"white\" stroke=\"black\"/><polygon points=\"2,26 26,26 35,16 11,16\" fill=\"white\" stroke=\"black\"/><polygon points=\"2,21 26,21 35,11 11,11\" fill=\"white\" stroke=\"black\"/><polygon points=\"2,16 26,16 35,6 11,6\" fill=\"white\" stroke=\"black\"/></svg>", "layerlist"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><polyline points=\"1,12 35,12\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"1,23 35,23\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"3,10 3,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"6,10 6,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"9,10 9,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"12,10 12,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"15,10 15,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"18,10 18,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"21,10 21,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"24,10 24,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"27,10 27,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"30,10 30,25\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"33,10 33,25\" stroke=\"black\" stroke-width=\"1\"/></svg>", "tracklist"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><polyline points=\"1,20 7.1,19.5 13,17.9 18.5,15.3 23.5,11.8 27.8,7.5\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/><polyline points=\"1,28 8.5,27.3 15.7,25.4 22.5,22.2 28.6,17.9 33.9,12.6\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/><polyline points=\"1,20 35,20\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"1,28 35,28\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"3,18 3,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"6,18 6,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"9,17 9,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"12,16 12,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"15,15 15,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"18,13 18,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"21,12 21,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"24,9 24,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"27,17 27,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"30,18 30,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"33,18 33,30\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"24,9 32,17\" stroke=\"black\" stroke-width=\"1\"/><polyline points=\"26,7 34,15\" stroke=\"black\" stroke-width=\"1\"/></svg>", "switchlist"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><polygon points=\"17,36 17,28 15,28 10,23 10,5 15,0 21,0 26,5 26,23 21,28 19,28 19,36\" fill=\"black\" /><circle cx=\"18\" cy=\"8\" r=\"4\" fill=\"red\" /><circle cx=\"18\" cy=\"20\" r=\"4\" fill=\"green\" /></svg>", "signallist"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><polyline points=\"1,20 10,20 30,15\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/><polyline points=\"28,17 28,20 34,20\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/></svg>", "accessorylist"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><polyline points=\"5,34 15,1\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/><polyline points=\"31,34 21,1\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/><polyline points=\"18,34 18,30\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/><polyline points=\"18,24 18,20\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/><polyline points=\"18,14 18,10\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/><polyline points=\"18,4 18,1\" stroke=\"black\" stroke-width=\"1\" fill=\"none\"/></svg>", "streetlist"));
		menuAdd.AddChildTag(HtmlTagButtonPopup("<svg width=\"36\" height=\"36\"><polyline points=\"1,25 35,25\" fill=\"none\" stroke=\"black\"/><polygon points=\"4,25 4,23 8,23 8,25\" fill=\"black\" stroke=\"black\"/><polygon points=\"35,22 16,22 15,19 18,10 35,10\" stroke=\"black\" fill=\"black\"/><polygon points=\"20,12 25,12 25,15 19,15\" fill=\"white\"/><polyline points=\"26,10 30,8 26,6\" stroke=\"black\" fill=\"none\"/><circle cx=\"22\" cy=\"22\" r=\"3\"/><circle cx=\"30\" cy=\"22\" r=\"3\"/></svg>", "feedbacklist"));
		menu.AddChildTag(menuAdd);
		body.AddChildTag(menu);

		body.AddChildTag(HtmlTag("div").AddClass("loco_selector").AddAttribute("id", "loco_selector").AddChildTag(HtmlTagLocoSelector()));
		body.AddChildTag(HtmlTag("div").AddClass("layer_selector").AddAttribute("id", "layer_selector").AddChildTag(HtmlTagLayerSelector()));
		body.AddChildTag(HtmlTag("div").AddClass("loco").AddAttribute("id", "loco"));
		body.AddChildTag(HtmlTag("div").AddClass("clock").AddAttribute("id", "clock").AddContent("<object data=\"/station-clock.svg\" class=\"clock2\" type=\"image/svg+xml\"><param name=\"secondHand\" value=\"din 41071.1\"/><param name=\"minuteHandBehavior\" value=\"sweeping\"/><param name=\"secondHandBehavior\" value=\"steeping\"/><param name=\"axisCoverRadius\" value=\"0\"/><param name=\"updateInterval\" value=\"250\"/></object>"));
		body.AddChildTag(HtmlTag("div").AddClass("layout").AddAttribute("id", "layout").AddAttribute("oncontextmenu", "return loadLayoutContext(event);"));
		body.AddChildTag(HtmlTag("div").AddClass("popup").AddAttribute("id", "popup"));
		body.AddChildTag(HtmlTag("div").AddClass("status").AddAttribute("id", "status"));
		body.AddChildTag(HtmlTag("div").AddClass("responses").AddAttribute("id", "responses"));

		body.AddChildTag(HtmlTag("div").AddClass("contextmenu").AddAttribute("id", "layout_context")
			.AddChildTag(HtmlTag("ul").AddClass("contextentries")
			.AddChildTag(HtmlTag("li").AddClass("contextentry").AddContent(Languages::GetText(Languages::TextAddTrack)).AddAttribute("onClick", "loadPopup('/?cmd=trackedit&track=0');"))
			.AddChildTag(HtmlTag("li").AddClass("contextentry").AddContent(Languages::GetText(Languages::TextAddSwitch)).AddAttribute("onClick", "loadPopup('/?cmd=switchedit&switch=0');"))
			.AddChildTag(HtmlTag("li").AddClass("contextentry").AddContent(Languages::GetText(Languages::TextAddSignal)).AddAttribute("onClick", "loadPopup('/?cmd=signaledit&signal=0');"))
			.AddChildTag(HtmlTag("li").AddClass("contextentry").AddContent(Languages::GetText(Languages::TextAddAccessory)).AddAttribute("onClick", "loadPopup('/?cmd=accessoryedit&accessory=0');"))
			.AddChildTag(HtmlTag("li").AddClass("contextentry").AddContent(Languages::GetText(Languages::TextAddStreet)).AddAttribute("onClick", "loadPopup('/?cmd=streetedit&street=0');"))
			.AddChildTag(HtmlTag("li").AddClass("contextentry").AddContent(Languages::GetText(Languages::TextAddFeedback)).AddAttribute("onClick", "loadPopup('/?cmd=feedbackedit&feedback=0');"))
			));

		connection->Send(HtmlFullResponse("Railcontrol", body));
	}
}; // namespace webserver
