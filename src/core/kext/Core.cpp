#include <IOKit/IOLib.h>
#include <sys/errno.h>

#include "ButtonStatus.hpp"
#include "CallBackWrapper.hpp"
#include "CommonData.hpp"
#include "Config.hpp"
#include "Core.hpp"
#include "EventInputQueue.hpp"
#include "EventOutputQueue.hpp"
#include "EventWatcher.hpp"
#include "GlobalLock.hpp"
#include "IOLogWrapper.hpp"
#include "KeyboardRepeat.hpp"
#include "ListHookedConsumer.hpp"
#include "ListHookedKeyboard.hpp"
#include "ListHookedPointing.hpp"
#include "NumHeldDownKeys.hpp"
#include "PressDownKeys.hpp"
#include "RemapClass.hpp"
#include "RemapFunc/PointingRelativeToScroll.hpp"
#include "RemapFunc/common/DependingPressingPeriodKeyToKey.hpp"
#include "TimerWrapper.hpp"
#include "VirtualKey.hpp"
#include "remap.hpp"

namespace org_pqrs_KeyRemap4MacBook {
  namespace Core {
    namespace {
      IOWorkLoop* workLoop = NULL;
    }

    void
    start(void)
    {
      GlobalLock::initialize();
      CommonData::initialize();
      EventWatcher::initialize();
      PressDownKeys::initialize();
      FlagStatus::initialize();
      ButtonStatus::initialize();

      workLoop = IOWorkLoop::workLoop();
      if (! workLoop) {
        IOLOG_ERROR("IOWorkLoop::workLoop failed\n");
      } else {
        ListHookedDevice::initializeAll(*workLoop);

        KeyboardRepeat::initialize(*workLoop);
        EventInputQueue::initialize(*workLoop);
        VirtualKey::initialize(*workLoop);
        EventOutputQueue::initialize(*workLoop);
        RemapFunc::DependingPressingPeriodKeyToKey::static_initialize(*workLoop);
        RemapFunc::PointingRelativeToScroll::static_initialize(*workLoop);
        ListHookedKeyboard::static_initialize(*workLoop);
        RemapClassManager::initialize(*workLoop);
      }

      Config::sysctl_register();
    }

    void
    stop(void)
    {
      // Destroy global lock.
      // Then, all callbacks and hooked functions become inactive.
      GlobalLock::terminate();

      // ------------------------------------------------------------
      ListHookedDevice::terminateAll();

      // ------------------------------------------------------------
      // call terminate
      Config::sysctl_unregister();

      RemapClassManager::terminate();
      KeyboardRepeat::terminate();
      EventInputQueue::terminate();
      VirtualKey::terminate();
      EventOutputQueue::terminate();
      RemapFunc::DependingPressingPeriodKeyToKey::static_terminate();
      RemapFunc::PointingRelativeToScroll::static_terminate();
      ListHookedKeyboard::static_terminate();

      if (workLoop) {
        workLoop->release();
        workLoop = NULL;
      }

      EventWatcher::terminate();
      PressDownKeys::terminate();

      CommonData::terminate();
    }

    // ======================================================================
    bool
    IOHIKeyboard_gIOMatchedNotification_callback(void* target, void* refCon, IOService* newService, IONotifier* notifier)
    {
      GlobalLock::ScopedLock lk;
      if (! lk) return false;

      IOLOG_DEBUG("%s newService:%p\n", __FUNCTION__, newService);

      IOHIDevice* device = OSDynamicCast(IOHIKeyboard, newService);
      if (! device) return false;

      ListHookedKeyboard::instance().push_back(new ListHookedKeyboard::Item(device));
      ListHookedConsumer::instance().push_back(new ListHookedConsumer::Item(device));
      return true;
    }

    bool
    IOHIKeyboard_gIOTerminatedNotification_callback(void* target, void* refCon, IOService* newService, IONotifier* notifier)
    {
      GlobalLock::ScopedLock lk;
      if (! lk) return false;

      IOLOG_DEBUG("%s newService:%p\n", __FUNCTION__, newService);

      IOHIDevice* device = OSDynamicCast(IOHIKeyboard, newService);
      if (! device) return false;

      ListHookedKeyboard::instance().erase(device);
      ListHookedConsumer::instance().erase(device);
      return true;
    }

    bool
    IOHIPointing_gIOMatchedNotification_callback(void* target, void* refCon, IOService* newService, IONotifier* notifier)
    {
      GlobalLock::ScopedLock lk;
      if (! lk) return false;

      IOLOG_DEBUG("%s newService:%p\n", __FUNCTION__, newService);

      IOHIDevice* device = OSDynamicCast(IOHIPointing, newService);
      if (! device) return false;

      ListHookedPointing::instance().push_back(new ListHookedPointing::Item(device));
      return true;
    }

    bool
    IOHIPointing_gIOTerminatedNotification_callback(void* target, void* refCon, IOService* newService, IONotifier* notifier)
    {
      GlobalLock::ScopedLock lk;
      if (! lk) return false;

      IOLOG_DEBUG("%s newService:%p\n", __FUNCTION__, newService);

      IOHIDevice* device = OSDynamicCast(IOHIPointing, newService);
      if (! device) return false;

      ListHookedPointing::instance().erase(device);
      return true;
    }

    // ======================================================================
    void
    remap_KeyboardEventCallback(ParamsUnion& paramsUnion)
    {
      if (paramsUnion.type != ParamsUnion::KEYBOARD) return;
      if (! paramsUnion.params.params_KeyboardEventCallBack) return;

      Params_KeyboardEventCallBack params = *(paramsUnion.params.params_KeyboardEventCallBack);
      RemapParams remapParams(paramsUnion);

      // ------------------------------------------------------------
      FlagStatus::set(params.key, params.flags);

      RemapClassManager::remap_key(remapParams);

      // ------------------------------------------------------------
      if (! remapParams.isremapped) {
        Params_KeyboardEventCallBack::auto_ptr ptr(Params_KeyboardEventCallBack::alloc(params.eventType, FlagStatus::makeFlags(), params.key,
                                                                                       params.charCode, params.charSet, params.origCharCode, params.origCharSet,
                                                                                       params.keyboardType, false));
        if (ptr) {
          KeyboardRepeat::set(*ptr);
          EventOutputQueue::FireKey::fire(*ptr);
        }
      }

      if (NumHeldDownKeys::iszero()) {
        NumHeldDownKeys::reset();
        KeyboardRepeat::cancel();
        EventWatcher::reset();
        FlagStatus::reset();
        ButtonStatus::reset();
        VirtualKey::reset();
        EventOutputQueue::FireModifiers::fire(FlagStatus::makeFlags());
        EventOutputQueue::FireRelativePointer::fire();
        PressDownKeys::clear();
      }

      RemapFunc::PointingRelativeToScroll::cancelScroll();
    }

    void
    remap_KeyboardSpecialEventCallback(ParamsUnion& paramsUnion)
    {
      if (paramsUnion.type != ParamsUnion::KEYBOARD_SPECIAL) return;
      if (! paramsUnion.params.params_KeyboardSpecialEventCallback) return;

      Params_KeyboardSpecialEventCallback params = *(paramsUnion.params.params_KeyboardSpecialEventCallback);
      RemapConsumerParams remapParams(paramsUnion);

      // ------------------------------------------------------------
      RemapClassManager::remap_consumer(remapParams);

      // ----------------------------------------
      if (! remapParams.isremapped) {
        Params_KeyboardSpecialEventCallback::auto_ptr ptr(Params_KeyboardSpecialEventCallback::alloc(params.eventType, FlagStatus::makeFlags(), params.key,
                                                                                                     params.flavor, params.guid, false));
        if (ptr) {
          KeyboardRepeat::set(*ptr);
          EventOutputQueue::FireConsumer::fire(*ptr);
        }
      }

      RemapFunc::PointingRelativeToScroll::cancelScroll();
    }

    void
    remap_RelativePointerEventCallback(ParamsUnion& paramsUnion)
    {
      if (paramsUnion.type != ParamsUnion::RELATIVE_POINTER) return;
      if (! paramsUnion.params.params_RelativePointerEventCallback) return;

      Params_RelativePointerEventCallback params = *(paramsUnion.params.params_RelativePointerEventCallback);
      RemapPointingParams_relative remapParams(paramsUnion);

      ButtonStatus::set(params.ex_button, params.ex_isbuttondown);

      RemapClassManager::remap_pointing(remapParams);

      // ------------------------------------------------------------
      if (! remapParams.isremapped) {
        EventOutputQueue::FireRelativePointer::fire(ButtonStatus::makeButtons(), params.dx, params.dy);
      }
    }

    void
    remap_ScrollWheelEventCallback(ParamsUnion& paramsUnion)
    {
      Params_ScrollWheelEventCallback* params = paramsUnion.get_Params_ScrollWheelEventCallback();
      if (! params) return;

      RemapPointingParams_scroll remapParams(paramsUnion);
      RemapClassManager::remap_pointing_scroll(remapParams);

      if (! remapParams.isremapped) {
        EventOutputQueue::FireScrollWheel::fire(*params);
        RemapFunc::PointingRelativeToScroll::cancelScroll();
      }
    }
  }
}
