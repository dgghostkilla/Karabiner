#include "base.hpp"
#include "ButtonStatus.hpp"
#include "Client.hpp"
#include "CommonData.hpp"
#include "Config.hpp"
#include "Core.hpp"
#include "RemapClass.hpp"
#include "remap.hpp"
#include "util/CallBackWrapper.hpp"
#include "util/EventInputQueue.hpp"
#include "util/EventOutput.hpp"
#include "util/KeyboardRepeat.hpp"
#include "util/ListHookedConsumer.hpp"
#include "util/ListHookedKeyboard.hpp"
#include "util/ListHookedPointing.hpp"
#include "util/NumHeldDownKeys.hpp"
#include "util/TimerWrapper.hpp"
#include "VirtualKey.hpp"

#include <sys/errno.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>

namespace org_pqrs_KeyRemap4MacBook {
  namespace Core {
    namespace {
      IOWorkLoop* workLoop = NULL;
      TimerWrapper timer_refresh;

      // ------------------------------------------------------------
      enum {
        REFRESH_DEVICE_INTERVAL = 3000,
      };

      void
      refreshHookedDevice(OSObject* owner, IOTimerEventSource* sender)
      {
        IOLockWrapper::ScopedLock lk(timer_refresh.getlock());

        ListHookedKeyboard::instance().refresh();
        ListHookedConsumer::instance().refresh();
        ListHookedPointing::instance().refresh();

        timer_refresh.setTimeoutMS(REFRESH_DEVICE_INTERVAL);
      }

      // ----------------------------------------
      void
      cancelRepeat(void)
      {
        if (config.debug_devel) {
          IOLog("KeyRemap4MacBook -Info- cancelRepeat\n");
        }

        KeyboardRepeat::cancel();
      }
    }

    void
    start(void)
    {
      EventWatcher::initialize();
      PressDownKeys::initialize();
      FlagStatus::initialize();
      ButtonStatus::initialize();
      KeyRemap4MacBook_client::initialize();
      CommonData::initialize();

      ListHookedKeyboard::instance().initialize();
      ListHookedConsumer::instance().initialize();
      ListHookedPointing::instance().initialize();

      workLoop = IOWorkLoop::workLoop();
      if (! workLoop) {
        IOLog("[KeyRemap4MacBook ERROR] IOWorkLoop::workLoop failed\n");
      } else {
        timer_refresh.initialize(workLoop, NULL, refreshHookedDevice);
        timer_refresh.setTimeoutMS(REFRESH_DEVICE_INTERVAL);

        RemapClassManager::initialize(*workLoop);
        KeyboardRepeat::initialize(*workLoop);
        EventInputQueue::initialize(*workLoop);
        VirtualKey::initialize(*workLoop);
        EventOutputQueue::initialize(*workLoop);
        HoldingKeyToKey::initialize(*workLoop);
      }

      sysctl_register();
    }

    void
    stop(void)
    {
      sysctl_unregister();

      timer_refresh.terminate();
      ListHookedKeyboard::instance().terminate();
      ListHookedConsumer::instance().terminate();
      ListHookedPointing::instance().terminate();
      RemapClassManager::terminate();
      KeyboardRepeat::terminate();
      EventInputQueue::terminate();
      VirtualKey::terminate();
      EventOutputQueue::terminate();
      HoldingKeyToKey::terminate();

      if (workLoop) {
        workLoop->release();
        workLoop = NULL;
      }

      CommonData::terminate();
      KeyRemap4MacBook_client::terminate();
      EventWatcher::terminate();
    }

    // ======================================================================
    bool
    notifierfunc_hookKeyboard(void* target, void* refCon, IOService* newService, IONotifier* notifier)
    {
      IOLog("KeyRemap4MacBook notifierfunc_hookKeyboard\n");

      IOHIKeyboard* kbd = OSDynamicCast(IOHIKeyboard, newService);
      ListHookedKeyboard::instance().append(kbd);
      ListHookedConsumer::instance().append(kbd);
      return true;
    }

    bool
    notifierfunc_unhookKeyboard(void* target, void* refCon, IOService* newService, IONotifier* notifier)
    {
      IOLog("KeyRemap4MacBook notifierfunc_unhookKeyboard\n");

      IOHIKeyboard* kbd = OSDynamicCast(IOHIKeyboard, newService);
      ListHookedKeyboard::instance().terminate(kbd);
      ListHookedConsumer::instance().terminate(kbd);
      return true;
    }

    bool
    notifierfunc_hookPointing(void* target, void* refCon, IOService* newService, IONotifier* notifier)
    {
      IOLog("KeyRemap4MacBook notifierfunc_hookPointing\n");

      IOHIPointing* pointing = OSDynamicCast(IOHIPointing, newService);
      return ListHookedPointing::instance().append(pointing);
    }

    bool
    notifierfunc_unhookPointing(void* target, void* refCon, IOService* newService, IONotifier* notifier)
    {
      IOLog("KeyRemap4MacBook notifierfunc_unhookPointing\n");

      IOHIPointing* pointing = OSDynamicCast(IOHIPointing, newService);
      return ListHookedPointing::instance().terminate(pointing);
    }

    // ======================================================================
    void
    remap_KeyboardEventCallback(Params_KeyboardEventCallBack& params)
    {
      params.log();

      // ------------------------------------------------------------
      RemapParams remapParams(params);

      // ------------------------------------------------------------
      FlagStatus::set(params.key, params.flags);

      RemapClassManager::remap_key(remapParams);

      // ------------------------------------------------------------
      if (! remapParams.isremapped) {
        params.flags = FlagStatus::makeFlags();
        KeyboardRepeat::set(params);
        EventOutput::FireKey::fire(params);
      }

      if (NumHeldDownKeys::iszero()) {
        NumHeldDownKeys::reset();
        cancelRepeat();
        EventWatcher::reset();
        FlagStatus::reset();
        ButtonStatus::reset();
        EventOutput::FireModifiers::fire(FlagStatus::makeFlags());
        EventOutput::FireRelativePointer::fire();
        PressDownKeys::clear();
      }
    }

    void
    remap_KeyboardSpecialEventCallback(Params_KeyboardSpecialEventCallback& params)
    {
      params.log();

      RemapConsumerParams remapParams(params);

      // ------------------------------------------------------------
      RemapClassManager::remap_consumer(remapParams);

      // ----------------------------------------
      if (! remapParams.isremapped) {
        params.flags = FlagStatus::makeFlags();
        KeyboardRepeat::set(params);
        EventOutput::FireConsumer::fire(params);
      }
    }

    void
    remap_RelativePointerEventCallback(Params_RelativePointerEventCallback& params)
    {
      params.log();

      RemapPointingParams_relative remapParams(params);

      RemapClassManager::remap_pointing(remapParams);

      // ------------------------------------------------------------
      if (! remapParams.isremapped) {
        EventOutput::FireRelativePointer::fire(ButtonStatus::makeButtons(), params.dx, params.dy);
      }
    }

    void
    remap_ScrollWheelEventCallback(Params_ScrollWheelEventCallback& params)
    {
      params.log();
      EventOutput::FireScrollWheel::fire(params);
    }
  }
}
