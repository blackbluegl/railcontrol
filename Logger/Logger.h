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

#pragma once

#include <string>

#include "Languages.h"
#include "Logger/LoggerServer.h"
#include "Network/TcpServer.h"

namespace Logger
{
	class Logger
	{
		public:
			Logger(LoggerServer& server, const std::string& component)
			:	server(server),
			 	component(component)
			{}

			~Logger() {};

			static Logger* GetLogger(const std::string& component) { return LoggerServer::Instance().GetLogger(component); }

			bool IsComponent(const std::string& component) { return component.compare(this->component) == 0; }

			static std::string Format(const std::string& input) { return input; }
			static std::string Format(char* input) { const char* constInput = input; return Format(constInput); }
			static std::string Format(const char* input)
			{
				if (input == nullptr)
				{
					return std::string("");
				}
				return std::string(input);
			}

			template<typename... Args>
			static std::string Format(const std::string& input, Args... args)
			{
				std::string output = input;
				FormatInternal(output, 0, args...);
				return output;
			}

			template<typename... Args>
			static std::string Format(char* input, Args... args)
			{
				const char* constInput = input;
				return Format(constInput, args...);
			}

			template<typename... Args>
			static std::string Format(const char* input, Args... args)
			{
				if (input == nullptr)
				{
					return std::string("");
				}
				return Format(std::string(input), args...);
			}

			template<typename... Args> void Error(const Languages::textSelector_t text, Args... args)
			{
				Log(std::string("Error"), Languages::GetText(text), args...);
			}

			template<typename... Args> void Error(const std::string& text, Args... args)
			{
				Log("Error", text, args...);
			}

			template<typename... Args> void Warning(const Languages::textSelector_t text, Args... args)
			{
				Log(std::string("Warning"), Languages::GetText(text), args...);
			}

			template<typename... Args> void Warning(const std::string& text, Args... args)
			{
				Log("Warning", text, args...);
			}

			template<typename... Args> void Info(const Languages::textSelector_t text, Args... args)
			{
				Log("Info", Languages::GetText(text), args...);
			}

			template<typename... Args> void Info(const std::string& text, Args... args)
			{
				Log("Info", text, args...);
			}

			template<typename... Args> void Debug(const Languages::textSelector_t text, Args... args)
			{
				Log(std::string("Debug"), Languages::GetText(text), args...);
			}

			template<typename... Args> void Debug(const std::string& text, Args... args)
			{
				Log("Debug", text, args...);
			}

			void Hex(const std::string& input) { Hex(reinterpret_cast<const unsigned char*>(input.c_str()), input.size()); }
			void Hex(const unsigned char* input, const size_t size);

			LoggerServer& server;
			const std::string component;

		private:
			static std::string DateTime();

			static void Replace(std::string& workString, const unsigned char argument, const std::string& value);
			static void Replace(std::string& workString, const unsigned char argument, char* value)
			{
				const char* constValue = value;
				Replace(workString, argument, std::string(constValue));
			}
			static void Replace(std::string& workString, const unsigned char argument, const char* value)
			{
				Replace(workString, argument, std::string(value == nullptr ? "" : value));
			}
			template<typename T>
			static void Replace(std::string& workString, const unsigned char argument, T value)
			{
				Replace(workString, argument, std::to_string(value));
			}

			template<typename T>
			static void FormatInternal(std::string& workString,
				const unsigned char argument,
				T value)
			{
				Replace(workString, argument, value);
			}

			template<typename T, typename... Args>
			static void FormatInternal(std::string& workString,
				const unsigned char argument,
				T value,
				Args... args)
			{
				Replace(workString, argument, value);
				FormatInternal(workString, argument + 1, args...);
			}

			template<typename... Args> void Log(const std::string& type, const std::string& text, Args... args)
			{
				server.Send(DateTime() + ": " + type + ": " + component + ": " + Format(text, args...) + "\n");
			}
	};
}
