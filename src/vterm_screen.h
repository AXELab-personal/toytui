// vterm_screen.h — the only gateway to the vendored libvterm (third_party/vterm).
//
// One instance = one emulated terminal screen: bytes written by a program go in
// (`write`), the screen content comes out as a toytui `Block` (`block`), keys given in
// the uniform notation of keycodec.h go in (`sendKey`) and come out as the bytes
// to be sent to the program (`takeOutput`; terminal replies to queries such as
// cursor-position requests are in the same stream).
//
// libvterm's own header is not visible to the rest of the code.
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "cell.h"

namespace toytui {

class VtermScreen {
public:
    // The terminal cursor: 0-based position, visibility, DECSCUSR shape code (1..6).
    struct Cursor {
        int row = 0;
        int col = 0;
        bool visible = true;
        int shape = 1;
    };

    // `rows`, `cols` >= 1 (smaller values are raised to 1).
    VtermScreen(int rows, int cols);
    ~VtermScreen();
    VtermScreen(const VtermScreen&) = delete;
    VtermScreen& operator=(const VtermScreen&) = delete;

    int rows() const;
    int cols() const;
    void resize(int rows, int cols);

    // Output of the program (bytes read from the pty).
    void write(const char* data, std::size_t size);

    // One key in the notation of keycodec.h (`a`, `é`, `[UP]`, `[CTRL-C]`, `[ALT-F3]`...).
    // false (and nothing queued) for an unknown notation.
    bool sendKey(const std::string& token);

    // Bytes destined for the program (keys, terminal replies); empties the queue.
    std::string takeOutput();

    // The visible screen as cells (rows x cols). Default colours/attributes are UNSET
    // (they inherit from the host element), wide characters are WIDE_LEFT + WIDE_RIGHT.
    Block block() const;

    Cursor cursor() const;
    bool altScreen() const;

    struct Impl; // implementation detail (defined in the .cpp)

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace toytui
