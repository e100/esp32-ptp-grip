// Host tests for the grip's input handling: binding engine, button and encoder filters.
#include "BindingEngine.h"
#include "InputFilters.h"
#include "grip_config.h"
#include <initializer_list>
#include <stdio.h>

using namespace grip;

static int g_fails = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, #c); g_fails++; } } while (0)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static BindingEngine makeDefault() {
    return BindingEngine(config::BINDINGS, COUNT(config::BINDINGS),
                         { config::PHOTO_SETTINGS, (uint8_t)COUNT(config::PHOTO_SETTINGS) },
                         { config::VIDEO_SETTINGS, (uint8_t)COUNT(config::VIDEO_SETTINGS) },
                         config::LAYER_COUNT);
}

// Default config indices: encoder(0) wheel, encoder(1) ISO dial;
// button(0) half, (1) full, (2) rec, (3) wheel push, (4) fn.
enum { HALF = 0, FULL = 1, REC = 2, PUSH = 3, FN = 4 };

static void testFixedAndSelectedEncoders() {
    printf("encoder bound to a fixed setting vs. the selected setting\n");
    BindingEngine e = makeDefault();
    Command c[BindingEngine::MAX_COMMANDS];

    CHECK(e.onEncoder(1, 2, c) == 1);                   // ISO dial
    CHECK(c[0].type == CommandType::StepSetting && c[0].setting == Setting::ISO && c[0].delta == 2);

    CHECK(e.onEncoder(0, -1, c) == 1);                  // wheel -> selected (Shutter first in photo)
    CHECK(c[0].setting == Setting::Shutter && c[0].delta == -1);

    CHECK(e.onButton(PUSH, ButtonEvent::Short, c) == 1);
    CHECK(c[0].type == CommandType::SelectionChanged && c[0].setting == Setting::Aperture);
    e.onEncoder(0, 1, c);
    CHECK(c[0].setting == Setting::Aperture);
    for (int i = 0; i < 4; i++) e.onButton(PUSH, ButtonEvent::Short, c);
    CHECK(e.selected() == Setting::Shutter);            // wrapped around the 5-item list

    CHECK(e.onEncoder(0, 0, c) == 0);                   // no movement, no command
}

static void testLayerHold() {
    printf("Fn hold switches layer; release restores it\n");
    BindingEngine e = makeDefault();
    Command c[BindingEngine::MAX_COMMANDS];

    CHECK(e.onButton(FN, ButtonEvent::Down, c) == 1);
    CHECK(c[0].type == CommandType::LayerChanged && c[0].delta == 1 && e.activeLayer() == 1);
    e.onEncoder(0, 1, c);
    CHECK(c[0].setting == Setting::ExposureComp);       // layer-1 row wins
    e.onEncoder(1, 1, c);
    CHECK(c[0].setting == Setting::ISO);                // LAYER_ANY row still applies
    CHECK(e.onButton(FN, ButtonEvent::Up, c) == 1);
    CHECK(e.activeLayer() == 0);
    e.onEncoder(0, 1, c);
    CHECK(c[0].setting == Setting::Shutter);
}

static void testShutterAndRecord() {
    printf("two-stage shutter and a separate REC button, in both modes\n");
    BindingEngine e = makeDefault();
    Command c[BindingEngine::MAX_COMMANDS];

    CHECK(e.onButton(HALF, ButtonEvent::Down, c) == 1 && c[0].type == CommandType::ShutterHalf && c[0].on);
    CHECK(e.onButton(FULL, ButtonEvent::Down, c) == 1 && c[0].type == CommandType::ShutterFull && c[0].on);
    CHECK(e.onButton(FULL, ButtonEvent::Up, c) == 1 && c[0].type == CommandType::ShutterFull && !c[0].on);
    CHECK(e.onButton(HALF, ButtonEvent::Up, c) == 1 && c[0].type == CommandType::ShutterHalf && !c[0].on);
    CHECK(e.onButton(REC, ButtonEvent::Short, c) == 1 && c[0].type == CommandType::RecordToggle);

    e.setCameraMode(CameraMode::Video);
    CHECK(e.onButton(REC, ButtonEvent::Short, c) == 1 && c[0].type == CommandType::RecordToggle);
    CHECK(e.onButton(HALF, ButtonEvent::Down, c) == 1 && c[0].type == CommandType::ShutterHalf);
    e.onEncoder(0, 1, c);
    CHECK(c[0].setting == Setting::ISO);                // video list starts with ISO
}

static void testReleaseFollowsPress() {
    printf("release goes to the binding that was pressed, even after a mode change\n");
    static const Binding b[] = {
        { button(0), Trigger::Hold, MODE_PHOTO, LAYER_ANY, shutterFull() },
        { button(0), Trigger::Hold, MODE_VIDEO, LAYER_ANY, shutterHalf() },
    };
    static const Setting none[] = { Setting::ISO };
    BindingEngine e(b, COUNT(b), { none, 1 }, { none, 1 }, 1);
    Command c[BindingEngine::MAX_COMMANDS];
    e.onButton(0, ButtonEvent::Down, c);                // full-press in photo
    e.setCameraMode(CameraMode::Video);                 // camera switches mode mid-press
    CHECK(e.onButton(0, ButtonEvent::Up, c) == 1);
    CHECK(c[0].type == CommandType::ShutterFull && !c[0].on);
    CHECK(e.onButton(0, ButtonEvent::Up, c) == 0);      // second release does nothing
}

static void testSelectionRememberedPerMode() {
    printf("selected setting is remembered separately for photo and video\n");
    BindingEngine e = makeDefault();
    Command c[BindingEngine::MAX_COMMANDS];
    e.onButton(PUSH, ButtonEvent::Short, c);            // photo: Aperture
    e.setCameraMode(CameraMode::Video);
    CHECK(e.selected() == Setting::ISO);
    e.onButton(PUSH, ButtonEvent::Short, c);            // video: Shutter
    e.setCameraMode(CameraMode::Photo);
    CHECK(e.selected() == Setting::Aperture);
}

static void testLockToggle() {
    printf("lock: only REC and unlock work; held buttons still release\n");
    BindingEngine e = makeDefault();
    Command c[BindingEngine::MAX_COMMANDS];
    e.onButton(HALF, ButtonEvent::Down, c);             // half-press held when locking
    CHECK(e.onButton(PUSH, ButtonEvent::Long, c) == 1);
    CHECK(c[0].type == CommandType::LockChanged && c[0].on && e.locked());

    CHECK(e.onEncoder(0, 1, c) == 0);
    CHECK(e.onEncoder(1, 1, c) == 0);
    CHECK(e.onButton(FULL, ButtonEvent::Down, c) == 0);
    CHECK(e.onButton(PUSH, ButtonEvent::Short, c) == 0);
    CHECK(e.onButton(FN, ButtonEvent::Down, c) == 0);
    CHECK(e.onButton(REC, ButtonEvent::Short, c) == 1 && c[0].type == CommandType::RecordToggle);
    CHECK(e.onButton(HALF, ButtonEvent::Up, c) == 1 && c[0].type == CommandType::ShutterHalf && !c[0].on);

    CHECK(e.onButton(PUSH, ButtonEvent::Long, c) == 1 && !c[0].on && !e.locked());
    CHECK(e.onEncoder(0, 1, c) == 1);
}

static void testLockSwitch() {
    printf("lock slide switch: locked while closed\n");
    static const Binding b[] = {
        { button(0), Trigger::Hold, MODE_ANY, LAYER_ANY, lock() },
        { encoder(0), Trigger::Turn, MODE_ANY, LAYER_ANY, adjust(Setting::ISO) },
    };
    static const Setting none[] = { Setting::ISO };
    BindingEngine e(b, COUNT(b), { none, 1 }, { none, 1 }, 1);
    Command c[BindingEngine::MAX_COMMANDS];
    CHECK(e.onButton(0, ButtonEvent::Down, c) == 1 && e.locked());
    CHECK(e.onEncoder(0, 1, c) == 0);
    CHECK(e.onButton(0, ButtonEvent::Up, c) == 1 && !e.locked());
    CHECK(e.onEncoder(0, 1, c) == 1);
}

static void testLayerNextAndUnbound() {
    printf("LayerNext cycles; unbound inputs produce nothing\n");
    static const Binding b[] = {
        { button(0), Trigger::Short, MODE_ANY, LAYER_ANY, layerNext() },
        { button(1), Trigger::Short, MODE_ANY, LAYER_ANY, selectPrev() },
        { encoder(0), Trigger::Turn, MODE_ANY, layer(2), adjust(Setting::WhiteBalance) },
    };
    static const Setting list[] = { Setting::ISO, Setting::Shutter, Setting::Aperture };
    BindingEngine e(b, COUNT(b), { list, 3 }, { list, 3 }, 3);
    Command c[BindingEngine::MAX_COMMANDS];
    CHECK(e.onEncoder(0, 1, c) == 0);                   // layer 0: unbound
    e.onButton(0, ButtonEvent::Short, c);
    e.onButton(0, ButtonEvent::Short, c);
    CHECK(e.activeLayer() == 2);
    CHECK(e.onEncoder(0, 1, c) == 1 && c[0].setting == Setting::WhiteBalance);
    e.onButton(0, ButtonEvent::Short, c);
    CHECK(e.activeLayer() == 0);                        // wraps
    CHECK(e.onEncoder(5, 1, c) == 0);                   // encoder not in table
    e.onButton(1, ButtonEvent::Short, c);               // selectPrev wraps to the end
    CHECK(e.selected() == Setting::Aperture);
    CHECK(e.onButton(40, ButtonEvent::Down, c) == 0);   // out of range
}

static void testButtonFilter() {
    printf("ButtonFilter: debounce, short, long\n");
    ButtonFilter f(20, 600);
    ButtonEvent ev[2];
    uint32_t t = 0;
    CHECK(f.update(true, t, ev) == 0);                  // bounce starts
    CHECK(f.update(false, t += 5, ev) == 0);
    CHECK(f.update(true, t += 5, ev) == 0);
    CHECK(f.update(true, t += 10, ev) == 0);            // only 10 ms stable
    CHECK(f.update(true, t += 10, ev) == 1 && ev[0] == ButtonEvent::Down);
    CHECK(f.update(true, t += 100, ev) == 0);
    CHECK(f.update(false, t += 1, ev) == 0);            // release bouncing
    CHECK(f.update(false, t += 25, ev) == 2 && ev[0] == ButtonEvent::Up && ev[1] == ButtonEvent::Short);

    CHECK(f.update(true, t += 100, ev) == 0);
    CHECK(f.update(true, t += 20, ev) == 1 && ev[0] == ButtonEvent::Down);
    CHECK(f.update(true, t += 599, ev) == 0);
    CHECK(f.update(true, t += 1, ev) == 1 && ev[0] == ButtonEvent::Long);
    CHECK(f.update(true, t += 1000, ev) == 0);          // Long fires once
    CHECK(f.update(false, t += 1, ev) == 0);
    CHECK(f.update(false, t += 20, ev) == 1 && ev[0] == ButtonEvent::Up);   // no Short after Long
}

static void testEncoderFilter() {
    printf("EncoderFilter: detents, direction changes, no jitter at rest, reverse\n");
    EncoderFilter f(4);
    CHECK(f.update(100) == 0);                          // first reading sets the origin (a rest position)
    CHECK(f.update(101) == 0);
    CHECK(f.update(102) == 1);                          // past halfway: the detent snaps over
    CHECK(f.update(104) == 0);
    CHECK(f.update(113) == 2);
    CHECK(f.update(109) == -1);                         // one click back from just past 112
    CHECK(f.update(101) == -2);
    for (int32_t c : { 99, 101, 100, 98, 101 })         // wobble around a resting click
        CHECK(f.update(c) == 0);
    CHECK(f.update(90) == -2);
    CHECK(f.update(-100) == -48);                       // big negative swing
    EncoderFilter r(2, true);
    r.update(0);
    CHECK(r.update(4) == -2);
}

int main() {
    testFixedAndSelectedEncoders();
    testLayerHold();
    testShutterAndRecord();
    testReleaseFollowsPress();
    testSelectionRememberedPerMode();
    testLockToggle();
    testLockSwitch();
    testLayerNextAndUnbound();
    testButtonFilter();
    testEncoderFilter();
    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
