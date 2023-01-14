/*
   Copyright 2022-2023 Nora Beda

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "pch.h"
#include "client.h"
#include "renderer.h"

namespace libchess::console {
    class client_callback_delegate {
    public:
        client_callback_delegate(client* _this) { m_this = _this; }
        ~client_callback_delegate() = default;

        client_callback_delegate(const client_callback_delegate&) = delete;
        client_callback_delegate& operator=(const client_callback_delegate&) = delete;

        void key_callback(char c) {
            util::mutex_lock lock(m_this->m_mutex);
            m_this->m_console->process_keystroke(c);
        }

    private:
        client* m_this = nullptr;
    };

    static void client_key_callback(char c, void* user_data) {
        client_callback_delegate delegate((client*)user_data);
        delegate.key_callback(c);
    }

    std::shared_ptr<client> client::create(const std::optional<std::string>& fen) {
        auto _client = std::shared_ptr<client>(new client);

        if (fen.has_value()) {
            if (!_client->load_fen(fen.value())) {
                _client.reset();
            }
        }

        if (_client) {
            _client->register_commands();
            _client->redraw();
            _client->m_console->set_accept_input(true);
        }

        return _client;
    }

    client::~client() { renderer::remove_key_callback(m_key_callback); }

    bool client::load_fen(const std::string& fen) {
        util::mutex_lock lock(m_mutex);

        auto _board = board::create(fen);
        if (!_board) {
            return false;
        }

        m_engine.set_board(_board);
        return true;
    }

    bool client::should_quit() {
        util::mutex_lock lock(m_mutex);
        return m_should_quit;
    }

    client::client() {
        auto _board = board::create_default();
        m_engine.set_board(_board);

        m_key_callback = renderer::add_key_callback(client_key_callback, this);
        m_should_quit = false;

        m_console = game_console::create();
    }

    class console_lock {
    public:
        console_lock(std::shared_ptr<game_console> console) {
            m_console = console;
            m_console->set_accept_input(false);
        }

        ~console_lock() { m_console->set_accept_input(true); }

        console_lock(const console_lock&) = delete;
        console_lock& operator=(const console_lock&) = delete;

    private:
        std::shared_ptr<game_console> m_console;
    };

#define BIND_CLIENT_COMMAND(func)                                                                  \
    [this](auto&&... args) -> decltype(auto) {                                                     \
        console_lock lock(m_console);                                                              \
        return func(std::forward<decltype(args)>(args)...);                                        \
    }

    void client::register_commands() {
        command_factory factory(m_console);

        // quit command
        factory.add_alias("quit");
        factory.set_callback(BIND_CLIENT_COMMAND(client::command_quit));
    }

    void client::redraw() {
        redraw_board(coord(0, 0));

        // todo: redraw console
    }

    void client::redraw_board(const coord& offset) {
        redraw_board_frame(offset);

        // drawing pieces
        for (int32_t x = 0; x < board::width; x++) {
            for (int32_t y = 0; y < board::width; y++) {
                auto local = coord(x, y);

                bool is_tile_white = local.taxicab_length() % 2 != 0;
                uint32_t fg = is_tile_white ? color_black : color_white;
                uint32_t bg = is_tile_white ? color_white : color_black;

                piece_info_t piece;
                m_engine.get_piece(local, &piece);

                char character = util::serialize_piece(piece).value_or(' ');
                auto global = offset + coord(1 + (x * 2), 1 + ((board::width - (y + 1)) * 2));

                renderer::render(global, (wchar_t)character, fg, bg);
            }
        }
    }

    void client::redraw_board_frame(const coord& offset) {
        // lines
        for (size_t i = 0; i <= board::width; i++) {
            for (size_t j = 0; j < board::width; j++) {
                int32_t coord_0 = (int32_t)i * 2;
                int32_t coord_1 = 1 + ((int32_t)j * 2);

                renderer::render(offset + coord(coord_0, coord_1), L'\x2551');
                renderer::render(offset + coord(coord_1, coord_0), L'\x2550');
            }
        }

        // intersections
        for (size_t i = 0; i < board::width - 1; i++) {
            for (size_t j = 0; j < board::width - 1; j++) {
                int32_t x = 2 + ((int32_t)i * 2);
                int32_t y = 2 + ((int32_t)j * 2);

                renderer::render(offset + coord(x, y), L'\x256c');
            }
        }

        // corners
        renderer::render(offset + coord(0, 0), L'\x2554');
        renderer::render(offset + coord(board::width * 2, 0), L'\x2557');
        renderer::render(offset + coord(0, board::width * 2), L'\x255a');
        renderer::render(offset + coord(board::width * 2, board::width * 2), L'\x255d');

        // edge intersections
        for (size_t i = 0; i < board::width - 1; i++) {
            int32_t c = 2 + ((int32_t)i * 2);

            renderer::render(offset + coord(c, 0), L'\x2566');
            renderer::render(offset + coord(c, board::width * 2), L'\x2569');
            renderer::render(offset + coord(0, c), L'\x2560');
            renderer::render(offset + coord(board::width * 2, c), L'\x2563');
        }
    }

    void client::command_quit(const std::vector<std::string>& args) { m_should_quit = true; }
} // namespace libchess::console