#include <unity.h>

#include "UsbDriveSession.h"

using knobify::usbdrive::UsbDriveEnd;
using knobify::usbdrive::UsbDrivePhase;
using knobify::usbdrive::UsbDriveSession;
using knobify::usbdrive::UsbStorage;

namespace {

class FakeStorage : public UsbStorage {
 public:
  bool startExport() override {
    ++starts;
    exported = canExport;
    return canExport;
  }
  void stopExport() override {
    ++stops;
    exported = false;
  }
  bool hostAttached() override { return attached; }
  bool takeEjectRequest() override {
    bool e = eject;
    eject = false;
    return e;
  }

  bool canExport = true;
  bool exported = false;
  bool attached = false;
  bool eject = false;
  int starts = 0;
  int stops = 0;
};

FakeStorage *storage;
UsbDriveSession *session;

}  // namespace

void setUp() {
  storage = new FakeStorage();
  session = new UsbDriveSession(*storage);
}

void tearDown() {
  delete session;
  delete storage;
}

void test_start_exports_and_waits_for_host() {
  TEST_ASSERT_TRUE(session->start(1000));
  TEST_ASSERT_TRUE(storage->exported);
  TEST_ASSERT_EQUAL(UsbDrivePhase::WaitingForHost, session->phase());

  // No computer: stays waiting, however long.
  session->tick(100000);
  TEST_ASSERT_EQUAL(UsbDrivePhase::WaitingForHost, session->phase());

  storage->attached = true;
  session->tick(100010);
  TEST_ASSERT_EQUAL(UsbDrivePhase::Connected, session->phase());
  TEST_ASSERT_FALSE(session->takeFinished());
}

void test_failed_export_stays_off() {
  storage->canExport = false;
  TEST_ASSERT_FALSE(session->start(0));
  TEST_ASSERT_FALSE(session->active());
  TEST_ASSERT_FALSE(session->takeFinished());
}

void test_eject_ends_session_once() {
  session->start(0);
  storage->attached = true;
  session->tick(10);
  storage->eject = true;
  session->tick(20);

  TEST_ASSERT_FALSE(session->active());
  TEST_ASSERT_FALSE(storage->exported);
  TEST_ASSERT_EQUAL(1, storage->stops);
  TEST_ASSERT_EQUAL(UsbDriveEnd::Ejected, session->lastEnd());
  TEST_ASSERT_TRUE(session->takeFinished());
  TEST_ASSERT_FALSE(session->takeFinished());
}

void test_host_gone_ends_session_after_grace_period() {
  session->start(0);
  storage->attached = true;
  session->tick(10);
  storage->attached = false;
  session->tick(10 + UsbDriveSession::kHostGoneMs - 1);
  TEST_ASSERT_TRUE(session->active());

  // A bus reset: back before the grace period ran out.
  storage->attached = true;
  session->tick(10 + UsbDriveSession::kHostGoneMs);
  storage->attached = false;
  session->tick(10 + 2 * UsbDriveSession::kHostGoneMs - 1);
  TEST_ASSERT_TRUE(session->active());

  session->tick(10 + 2 * UsbDriveSession::kHostGoneMs);
  TEST_ASSERT_FALSE(session->active());
  TEST_ASSERT_EQUAL(UsbDriveEnd::HostGone, session->lastEnd());
  TEST_ASSERT_TRUE(session->takeFinished());
}

void test_done_while_waiting_and_repeated_finish() {
  session->start(0);
  session->finish();
  session->finish();
  TEST_ASSERT_EQUAL(1, storage->stops);
  TEST_ASSERT_EQUAL(UsbDriveEnd::Done, session->lastEnd());
  TEST_ASSERT_TRUE(session->takeFinished());

  // A second session starts cleanly.
  TEST_ASSERT_TRUE(session->start(5000));
  TEST_ASSERT_EQUAL(2, storage->starts);
  TEST_ASSERT_EQUAL(UsbDrivePhase::WaitingForHost, session->phase());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_exports_and_waits_for_host);
  RUN_TEST(test_failed_export_stays_off);
  RUN_TEST(test_eject_ends_session_once);
  RUN_TEST(test_host_gone_ends_session_after_grace_period);
  RUN_TEST(test_done_while_waiting_and_repeated_finish);
  return UNITY_END();
}
