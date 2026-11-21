#include "../source/chams_inprocess_probe.hpp"
#include <cassert>
int main() {
    const auto r = reshade::chams::inprocess_probe::run();
    assert(r.pid == GetCurrentProcessId());
    assert(r.local_marker_ok && r.marker_error == ERROR_SUCCESS);
    assert(r.host_image_header_ok && r.image_error == ERROR_SUCCESS);
    assert(!r.is_hd_player); // This test is not the BlueStacks executable.
}
