//
// Copyright (c) 2025 fasxmut
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <memory>
#include <functional>
#include <chrono>
#include <sstream>
#include <mutex>
#include <thread>

using std::string_literals::operator""s;

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace http = beast::http;

namespace eghost::host_config
{
	constexpr std::string_view name = "eghost";
	constexpr std::string_view url_prefix = "https://";
	constexpr std::string_view url_host = "www.boost.org";
}

namespace eghost
{

	std::mutex out_mutex;	// for std::cout, std::cerr, std::clog all.

	class static_page_class
	{
	private:
		const std::string __url_prefix;
		const std::string __url_host;
	public:
		const std::string html;
	public:
		static_page_class(const std::string_view url_prefix__, const std::string_view & url_host__):
			__url_prefix{url_prefix__},
			__url_host{url_host__},
			html{
				"<!DOCTYPE html>\n"s +
				"<html>\n" +
				"<head>\n" +
				"<title>" + eghost::host_config::name + "</title>\n" +
				"<body>\n" +
				"<center>\n" +
				"<h1>" + eghost::host_config::name + "</h1>\n" +
				"<p>Example http web server.</p>\n" +
				"<hr />\n" +
				"<p>\n" +
				"<a href=\"" + __url_prefix + __url_host + "\">" + __url_host + "</a>\n" +
				"</p>\n" +
				"</center>\n" + 
				"</body>\n" +
				"</html>\n"
			}
		{
		}
	};

	class static_response_class
	{
	public:
		http::response<http::string_body> response;
	public:
		constexpr static_response_class(const eghost::static_page_class & static_page)
		{
			response.body() = static_page.html;
			response.set(http::field::server, eghost::host_config::name);
			response.set(http::field::content_type, "text/html");
			response.set("Host", eghost::host_config::url_host);
		}
	};

///////////////////////////////////////////////////////////////////////////
	const inline static eghost::static_page_class
		static_page
			{
				eghost::host_config::url_prefix,
				eghost::host_config::url_host
			};
	inline static eghost::static_response_class static_response{eghost::static_page};
}

namespace eghost
{

	class session: virtual public std::enable_shared_from_this<eghost::session>
	{
	private:
		beast::tcp_stream __stream;
		http::request<http::string_body> __request;
		beast::flat_buffer __buffer;
	public:
		virtual ~session()
		{
			__stream.close();
			std::unique_lock<std::mutex> lock{eghost::out_mutex};
			std::clog << "A session is closed." << std::endl << std::endl;
		}
	public:
		session(asio::ip::tcp::socket && socket__):
			__stream{std::move(socket__)}
		{
			std::unique_lock<std::mutex> lock{eghost::out_mutex};
			std::clog << "A session is created." << std::endl;
		}
	public:
		asio::awaitable<void> run()
		{
			co_await this->read_write();
		}
	protected:
		asio::awaitable<void> read_write()
		{
			co_await http::async_read(__stream, __buffer, __request, asio::use_awaitable);

			{
				std::unique_lock<std::mutex> lock{eghost::out_mutex};
				std::clog << std::endl;
				std::clog << std::endl;
				std::clog << "------------------------------------------------------------\n";
				std::clog << "---------- Request: ----------\n";
				std::clog << __request << "\n";
				std::clog << "------------------------------------------------------------\n";
				std::clog << std::endl;
				std::clog << std::endl;
			}

			////////
			static_response.response.keep_alive(__request.keep_alive());
			static_response.response.prepare_payload();
			{
				std::ostringstream time_buff;
				time_buff << std::chrono::system_clock::now();
				static_response.response.set("Date-Time", time_buff.str());
			}
			////////

			co_await http::async_write(__stream, static_response.response, asio::use_awaitable);
			{
				std::clog << std::endl;
				std::clog << std::endl;
				std::clog << "------------------------------------------------------------\n";
				std::clog << "---------- Responsed: ----------\n";
				std::clog << static_response.response << "\n";
				std::clog << "------------------------------------------------------------\n";
				std::clog << std::endl;
				std::clog << std::endl;
			}
		}
	};

	class server: virtual public std::enable_shared_from_this<eghost::server>
	{
	private:
		const std::string __address;
		const unsigned short __port;
		asio::ip::tcp::acceptor __acceptor;
	public:
		server(
			asio::any_io_executor executor__,
			const std::string address__,
			const unsigned short port__
		):
			__address{address__},
			__port{port__},
			__acceptor{
				executor__,
				asio::ip::tcp::endpoint{
					asio::ip::make_address(__address),
					__port
				}
			}
		{
		}
	public:
		asio::awaitable<void> run()
		{
			co_await this->accept();
		}
	private:
		asio::awaitable<void> accept()
		{
			asio::ip::tcp::socket socket = co_await __acceptor.async_accept(
				asio::use_awaitable
			);
			co_await std::make_shared<eghost::session>(std::move(socket))->run();
			co_await this->accept();
		}
	};
}

int main(int argc, char * argv[])
{
	try
	{
		if (argc != 3)
			throw std::runtime_error{"http_server <bind address> <bind port>"};

		asio::io_context io_context;
		asio::co_spawn(
			io_context,
			[host=std::string{argv[1]}, port=static_cast<unsigned short>(std::stoi(argv[2]))] ()
				-> asio::awaitable<void>
			{
				auto server = std::make_shared<eghost::server>(
					co_await asio::this_coro::executor,
					host,
					port
				);
				co_await server->run();
			},
			[] (std::exception_ptr eptr)
			{
				if (eptr)
					std::rethrow_exception(eptr);
			}
		);
		io_context.run();
	}
	catch (const std::exception & e)
	{
		std::unique_lock<std::mutex> lock{eghost::out_mutex};
		std::cerr << "eghost server error:\n" << e.what() << "..." << std::endl;
	}
}
