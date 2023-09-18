#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

void fail(beast::error_code ec, char const *what)
{
	std::cerr << "what"
			  << ": " << ec.message() << "\n";
}

class Session : public std::enable_shared_from_this<Session>
{
	websocket::stream<beast::tcp_stream> ws_;
	beast::flat_buffer buffer_;

public:
	explicit Session(tcp::socket &&socket)
		: ws_(std::move(socket))
	{
	}

	void run()
	{
		ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

		ws_.set_option(websocket::stream_base::decorator(
			[](websocket::request_type &res)
			{
				res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server-async");
			}));

		ws_.async_accept(beast::bind_front_handler(&Session::on_accept, shared_from_this()));
	}

	void on_accept(beast::error_code ec)
	{
		if (ec)
		{
			return fail(ec, "accept");
		}

		do_read();
	}

	void do_read()
	{
		ws_.async_read(buffer_, beast::bind_front_handler(&Session::on_read, shared_from_this()));
	}

	void on_read(beast::error_code ec, std::size_t bytes_transferred)
	{
		boost::ignore_unused(bytes_transferred);

		if (ec == websocket::error::closed)
		{
			return;
		}

		if (ec)
		{
			fail(ec, "read");
		}

		ws_.text(ws_.got_text());
		ws_.async_write(
			buffer_.data(),
			beast::bind_front_handler(&Session::on_write, shared_from_this()));
	}

	void on_write(beast::error_code ec, std::size_t bytes_transferred)
	{
		boost::ignore_unused(bytes_transferred);

		if (ec)
		{
			return fail(ec, "ec");
		}

		buffer_.consume(buffer_.size());

		do_read();
	}
};

class Listener : public std::enable_shared_from_this<Listener>
{
	net::io_context &ioc_;
	tcp::acceptor acceptor_;

public:
	Listener(net::io_context &ioc, tcp::endpoint endpoint) : ioc_(ioc), acceptor_(ioc)
	{
		beast::error_code ec;

		acceptor_.open(endpoint.protocol(), ec);
		if (ec)
		{
			fail(ec, "open");
			return;
		}

		acceptor_.set_option(net::socket_base::reuse_address(true), ec);
		if (ec)
		{
			fail(ec, "set_option");
			return;
		}

		acceptor_.bind(endpoint, ec);
		if (ec)
		{
			fail(ec, "bind");
			return;
		}

		acceptor_.listen(net::socket_base::max_listen_connections, ec);
		if (ec)
		{
			fail(ec, "listen");
			return;
		}
	}

	void run()
	{
		do_accept();
	}

private:
	void do_accept()
	{
		acceptor_.async_accept(
			net::make_strand(ioc_),
			beast::bind_front_handler(&Listener::on_accept, shared_from_this()));
	}

	void on_accept(beast::error_code ec, tcp::socket socket)
	{
		std::cout << "Accepted connection";

		if (ec)
		{
			fail(ec, "accept");
		}
		else
		{
			std::make_shared<Session>(std::move(socket))->run();
		}

		do_accept();
	}
};

int main(int argc, char *argv[])
{
	if (argc != 4)
	{
		std::cerr << "Usage: websocket-server-async <address> <port> <threads>\n"
				  << "Example:\n"
				  << "      websocket-server-async 0.0.0.0 8080 1\n";
		return EXIT_FAILURE;
	}

	auto const address = net::ip::make_address(argv[1]);
	auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
	auto const threads_count = std::max<int>(1, std::atoi(argv[3]));

	net::io_context ioc{threads_count};
	std::make_shared<Listener>(ioc, tcp::endpoint{address, port})->run();

	std::vector<std::thread> threads;
	threads.reserve(threads_count - 1);

	for (auto i = threads_count - 1; i > 0; --i)
	{
		threads.emplace_back(
			[&ioc]
			{ ioc.run(); });
	}

	std::cout << "server is started";

	ioc.run();

	return 0;
}
