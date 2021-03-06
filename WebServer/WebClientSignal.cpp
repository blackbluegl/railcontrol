/*
RailControl - Model Railway Control Software

Copyright (c) 2017-2020 Dominik (Teddy) Mahrer - www.railcontrol.org

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

#include <vector>

#include "DataModel/ObjectIdentifier.h"
#include "DataModel/Signal.h"
#include "DataModel/TrackBase.h"
#include "Utils/Utils.h"
#include "WebServer/HtmlTag.h"
#include "WebServer/HtmlTagButtonCancel.h"
#include "WebServer/HtmlTagButtonCommandWide.h"
#include "WebServer/HtmlTagButtonOK.h"
#include "WebServer/HtmlTagButtonPopupWide.h"
#include "WebServer/HtmlTagInputCheckboxWithLabel.h"
#include "WebServer/HtmlTagInputIntegerWithLabel.h"
#include "WebServer/HtmlTagInputHidden.h"
#include "WebServer/HtmlTagInputTextWithLabel.h"
#include "WebServer/HtmlTagSelect.h"
#include "WebServer/HtmlTagSelectWithLabel.h"
#include "WebServer/HtmlTagSelectOrientationWithLabel.h"
#include "WebServer/HtmlTagSignal.h"
#include "WebServer/WebClient.h"
#include "WebServer/WebClientCluster.h"
#include "WebServer/WebClientSignal.h"

using namespace DataModel;
using LayoutPosition = DataModel::LayoutItem::LayoutPosition;
using LayoutItemSize = DataModel::LayoutItem::LayoutItemSize;
using LayoutRotation = DataModel::LayoutItem::LayoutRotation;
using std::map;
using std::string;
using std::to_string;
using std::vector;

namespace WebServer
{
	void WebClientSignal::HandleSignalEdit(const map<string, string>& arguments)
	{
		HtmlTag content;
		SignalID signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);
		ControlID controlID = manager.GetControlForAccessory();
		Protocol protocol = ProtocolNone;
		Address address = AddressNone;
		string name = Languages::GetText(Languages::TextNew);
		Orientation signalOrientation = static_cast<Orientation>(Utils::Utils::GetBoolMapEntry(arguments, "signalorientation", OrientationRight));
		LayoutPosition posx = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		LayoutPosition posy = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		LayoutPosition posz = Utils::Utils::GetIntegerMapEntry(arguments, "posz", LayerUndeletable);
		LayoutItemSize height = Utils::Utils::GetIntegerMapEntry(arguments, "length", 1);
		LayoutRotation rotation = static_cast<LayoutRotation>(Utils::Utils::GetIntegerMapEntry(arguments, "rotation", DataModel::LayoutItem::Rotation0));
		DataModel::AccessoryType signalType = DataModel::SignalTypeSimpleLeft;
		DataModel::AccessoryPulseDuration duration = manager.GetDefaultAccessoryDuration();
		bool inverted = false;
		std::vector<FeedbackID> feedbacks;
		DataModel::SelectRouteApproach selectRouteApproach = static_cast<DataModel::SelectRouteApproach>(Utils::Utils::GetIntegerMapEntry(arguments, "selectrouteapproach", DataModel::SelectRouteSystemDefault));
		bool allowLocoTurn = Utils::Utils::GetBoolMapEntry(arguments, "allowlocoturn", false);
		bool releaseWhenFree = Utils::Utils::GetBoolMapEntry(arguments, "releasewhenfree", false);
		Cluster* cluster = nullptr;
		if (signalID > SignalNone)
		{
			const DataModel::Signal* signal = manager.GetSignal(signalID);
			if (signal != nullptr)
			{
				controlID = signal->GetControlID();
				protocol = signal->GetProtocol();
				address = signal->GetAddress();
				name = signal->GetName();
				signalOrientation = signal->GetSignalOrientation();
				posx = signal->GetPosX();
				posy = signal->GetPosY();
				posz = signal->GetPosZ();
				height = signal->GetHeight();
				rotation = signal->GetRotation();
				signalType = signal->GetType();
				duration = signal->GetAccessoryPulseDuration();
				inverted = signal->GetInverted();
				feedbacks = signal->GetFeedbacks();
				cluster = signal->GetCluster();
				selectRouteApproach = signal->GetSelectRouteApproach();
				allowLocoTurn = signal->GetAllowLocoTurn();
				releaseWhenFree = signal->GetReleaseWhenFree();
			}
		}

		std::map<DataModel::AccessoryType, Languages::TextSelector> signalTypeOptions;
		signalTypeOptions[DataModel::SignalTypeSimpleLeft] = Languages::TextSimpleLeft;
		signalTypeOptions[DataModel::SignalTypeSimpleRight] = Languages::TextSimpleRight;

		content.AddChildTag(HtmlTag("h1").AddContent(name).AddId("popup_title"));
		HtmlTag tabMenu("div");
		tabMenu.AddChildTag(client.HtmlTagTabMenuItem("main", Languages::TextBasic, true));
		tabMenu.AddChildTag(client.HtmlTagTabMenuItem("position", Languages::TextPosition));
		tabMenu.AddChildTag(client.HtmlTagTabMenuItem("feedback", Languages::TextFeedbacks));
		tabMenu.AddChildTag(client.HtmlTagTabMenuItem("automode", Languages::TextAutomode));
		content.AddChildTag(tabMenu);

		HtmlTag formContent;
		formContent.AddChildTag(HtmlTagInputHidden("cmd", "signalsave"));
		formContent.AddChildTag(HtmlTagInputHidden("signal", to_string(signalID)));

		HtmlTag mainContent("div");
		mainContent.AddId("tab_main");
		mainContent.AddClass("tab_content");
		mainContent.AddChildTag(HtmlTagInputTextWithLabel("name", Languages::TextName, name).AddAttribute("onkeyup", "updateName();"));
		mainContent.AddChildTag(HtmlTagSelectOrientationWithLabel("signalorientation", Languages::TextOrientation, signalOrientation));
		mainContent.AddChildTag(HtmlTagSelectWithLabel("signaltype", Languages::TextType, signalTypeOptions, signalType));
		mainContent.AddChildTag(HtmlTagInputIntegerWithLabel("length", Languages::TextLength, height, DataModel::Signal::MinLength, DataModel::Signal::MaxLength));
		mainContent.AddChildTag(client.HtmlTagControlAccessory(controlID, "signal", signalID));
		mainContent.AddChildTag(HtmlTag("div").AddId("select_protocol").AddChildTag(client.HtmlTagProtocolAccessory(controlID, protocol)));
		mainContent.AddChildTag(HtmlTagInputIntegerWithLabel("address", Languages::TextAddress, address, 1, 2044));
		mainContent.AddChildTag(client.HtmlTagDuration(duration));
		mainContent.AddChildTag(HtmlTagInputCheckboxWithLabel("inverted", Languages::TextInverted, "true", inverted));
		formContent.AddChildTag(mainContent);

		formContent.AddChildTag(client.HtmlTagTabPosition(posx, posy, posz, rotation));

		formContent.AddChildTag(HtmlTagTabTrackFeedback(client, feedbacks, ObjectIdentifier(ObjectTypeSignal, signalID)));

		formContent.AddChildTag(HtmlTagTabTrackAutomode(selectRouteApproach, allowLocoTurn, releaseWhenFree, cluster));

		content.AddChildTag(HtmlTag("div").AddClass("popup_content").AddChildTag(HtmlTag("form").AddId("editform").AddChildTag(formContent)));
		content.AddChildTag(HtmlTagButtonCancel());
		content.AddChildTag(HtmlTagButtonOK());
		client.ReplyHtmlWithHeader(content);
	}

	void WebClientSignal::HandleSignalSave(const map<string, string>& arguments)
	{
		SignalID signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);
		string name = Utils::Utils::GetStringMapEntry(arguments, "name");
		Orientation signalOrientation = static_cast<Orientation>(Utils::Utils::GetBoolMapEntry(arguments, "signalorientation", OrientationRight));
		ControlID controlId = Utils::Utils::GetIntegerMapEntry(arguments, "control", ControlIdNone);
		Protocol protocol = static_cast<Protocol>(Utils::Utils::GetIntegerMapEntry(arguments, "protocol", ProtocolNone));
		Address address = Utils::Utils::GetIntegerMapEntry(arguments, "address", AddressNone);
		LayoutPosition posX = Utils::Utils::GetIntegerMapEntry(arguments, "posx", 0);
		LayoutPosition posY = Utils::Utils::GetIntegerMapEntry(arguments, "posy", 0);
		LayoutPosition posZ = Utils::Utils::GetIntegerMapEntry(arguments, "posz", 0);
		LayoutItemSize height = Utils::Utils::GetIntegerMapEntry(arguments, "length", 1);
		LayoutRotation rotation = static_cast<LayoutRotation>(Utils::Utils::GetIntegerMapEntry(arguments, "rotation", DataModel::LayoutItem::Rotation0));
		vector<FeedbackID> feedbacks;
		unsigned int feedbackCounter = Utils::Utils::GetIntegerMapEntry(arguments, "feedbackcounter", 1);
		for (unsigned int feedback = 1; feedback <= feedbackCounter; ++feedback)
		{
			FeedbackID feedbackID = Utils::Utils::GetIntegerMapEntry(arguments, "feedback_" + to_string(feedback), FeedbackNone);
			if (feedbackID != FeedbackNone)
			{
				feedbacks.push_back(feedbackID);
			}
		}
		DataModel::SelectRouteApproach selectRouteApproach = static_cast<DataModel::SelectRouteApproach>(Utils::Utils::GetIntegerMapEntry(arguments, "selectrouteapproach", DataModel::SelectRouteSystemDefault));
		bool allowLocoTurn = Utils::Utils::GetBoolMapEntry(arguments, "allowlocoturn", false);
		bool releaseWhenFree = Utils::Utils::GetBoolMapEntry(arguments, "releasewhenfree", false);
		DataModel::AccessoryType signalType = static_cast<DataModel::AccessoryType>(Utils::Utils::GetIntegerMapEntry(arguments, "signaltype", DataModel::SignalTypeSimpleLeft));
		DataModel::AccessoryPulseDuration duration = Utils::Utils::GetIntegerMapEntry(arguments, "duration", manager.GetDefaultAccessoryDuration());
		bool inverted = Utils::Utils::GetBoolMapEntry(arguments, "inverted");
		string result;
		if (!manager.SignalSave(signalID,
			name,
			signalOrientation,
			posX,
			posY,
			posZ,
			height,
			rotation,
			feedbacks,
			selectRouteApproach,
			allowLocoTurn,
			releaseWhenFree,
			controlId,
			protocol,
			address,
			signalType,
			duration,
			inverted,
			result))
		{
			client.ReplyResponse(WebClient::ResponseError, result);
			return;
		}

		client.ReplyResponse(WebClient::ResponseInfo, Languages::TextSignalSaved, name);
	}

	void WebClientSignal::HandleSignalState(const map<string, string>& arguments)
	{
		SignalID signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);
		DataModel::AccessoryState signalState = (Utils::Utils::GetStringMapEntry(arguments, "state", "red").compare("red") == 0 ? DataModel::SignalStateStop : DataModel::SignalStateClear);

		manager.SignalState(ControlTypeWebserver, signalID, signalState, false);

		client.ReplyHtmlWithHeaderAndParagraph(signalState ? Languages::TextSignalStateIsClear : Languages::TextSignalStateIsStop, manager.GetSignalName(signalID));
	}

	void WebClientSignal::HandleSignalList()
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
			const string& signalIdString = to_string(signal.second->GetID());
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
		client.ReplyHtmlWithHeader(content);
	}

	void WebClientSignal::HandleSignalAskDelete(const map<string, string>& arguments)
	{
		SignalID signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);

		if (signalID == SignalNone)
		{
			client.ReplyHtmlWithHeaderAndParagraph(Languages::TextSignalDoesNotExist);
			return;
		}

		const DataModel::Signal* signal = manager.GetSignal(signalID);
		if (signal == nullptr)
		{
			client.ReplyHtmlWithHeaderAndParagraph(Languages::TextSignalDoesNotExist);
			return;
		}

		HtmlTag content;
		const string& signalName = signal->GetName();
		content.AddContent(HtmlTag("h1").AddContent(Languages::TextDeleteSignal));
		content.AddContent(HtmlTag("p").AddContent(Languages::TextAreYouSureToDelete, signalName));
		content.AddContent(HtmlTag("form").AddId("editform")
			.AddContent(HtmlTagInputHidden("cmd", "signaldelete"))
			.AddContent(HtmlTagInputHidden("signal", to_string(signalID))
			));
		content.AddContent(HtmlTagButtonCancel());
		content.AddContent(HtmlTagButtonOK());
		client.ReplyHtmlWithHeader(content);
	}

	void WebClientSignal::HandleSignalDelete(const map<string, string>& arguments)
	{
		SignalID signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal", SignalNone);
		const DataModel::Signal* signal = manager.GetSignal(signalID);
		if (signal == nullptr)
		{
			client.ReplyResponse(WebClient::ResponseError, Languages::TextSignalDoesNotExist);
			return;
		}

		string result;
		if (!manager.SignalDelete(signalID, result))
		{
			client.ReplyResponse(WebClient::ResponseError, result);
			return;
		}

		string name = signal->GetName();
		client.ReplyResponse(WebClient::ResponseInfo, Languages::TextSignalDeleted, name);
	}

	void WebClientSignal::HandleSignalGet(const map<string, string>& arguments)
	{
		SignalID signalID = Utils::Utils::GetIntegerMapEntry(arguments, "signal");
		const DataModel::Signal* signal = manager.GetSignal(signalID);
		if (signal == nullptr)
		{
			client.ReplyHtmlWithHeader(HtmlTag());
			return;
		}
		client.ReplyHtmlWithHeader(HtmlTagSignal(manager, signal));
	}

	void WebClientSignal::HandleSignalRelease(const map<string, string>& arguments)
	{
		const ObjectIdentifier identifier(ObjectTypeSignal, Utils::Utils::GetIntegerMapEntry(arguments, "signal"));
		bool ret = manager.TrackBaseRelease(identifier);
		client.ReplyHtmlWithHeaderAndParagraph(ret ? "Signal released" : "Signal not released");
	}
} // namespace WebServer
