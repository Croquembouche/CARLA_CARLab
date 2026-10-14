// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/multigpu/listener.h"
#include "carla/multigpu/primary.h"

#include "carla/Logging.h"

#include <boost/asio/post.hpp>

#include <memory>

namespace carla {
namespace multigpu {

  Listener::Listener(boost::asio::io_context &io_context, endpoint ep)
    : _io_context(io_context),
      _acceptor(_io_context, std::move(ep)),
      _timeout(time_duration::seconds(1u)) {
        _acceptor.listen();
      }

  Listener::~Listener() {
    Stop();
  }
  
  void Listener::Stop() {
    if (_stopping.exchange(true)) return;
    boost::system::error_code ec;
    _acceptor.cancel(ec);
    _acceptor.close(ec);
    // The router owns the context and joins its workers before this teardown.
    // A listener must never restart a context with active run() calls.
  }
  
  void Listener::OpenSession(
      time_duration timeout,
      callback_function_type on_opened,
      callback_function_type on_closed,
      callback_function_type_response on_response) {

    using boost::system::error_code;
    if (_stopping.load()) return;

    auto session = std::make_shared<Primary>(_io_context, timeout, *this);
    auto self = shared_from_this();
    
    auto handle_query = [on_opened, on_closed, on_response, session, self](const error_code &ec)
    {
      if (!ec && !self->_stopping.load()) {
        session->Open(std::move(on_opened), std::move(on_closed), std::move(on_response));
      } else if (ec && ec != boost::asio::error::operation_aborted && !self->_stopping.load()) {
        log_error("Secondary server:", ec.message());
      }
    };

    _acceptor.async_accept(
      session->_socket,
      [self, session, handle_query, timeout, on_opened, on_closed, on_response](error_code ec)
      {
        if (self->_stopping.load() || ec == boost::asio::error::operation_aborted) return;
        boost::asio::post(
          session->_strand,
          [ec, handle_query]()
          {
            handle_query(ec);
          });

        self->OpenSession(timeout, on_opened, on_closed, on_response);
      });
  }

} // namespace multigpu
} // namespace carla
