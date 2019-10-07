#pragma once

#include <string>
#include <thread>
#include <vector>

#include "Network/TcpConnection.h"

namespace Network
{
	class TcpServer
	{
		protected:
			TcpServer(const unsigned short port, const std::string& threadName);
			~TcpServer();
			void TerminateTcpServer();

			virtual void Work(Network::TcpConnection* connection) = 0;

		private:
			void Worker();

			unsigned short port;
			int serverSocket;
			volatile bool run;
			std::thread serverThread;
			std::vector<TcpConnection*> connections;
			std::string error;
			const std::string threadName;
	};
}