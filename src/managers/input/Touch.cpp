#include "InputManager.hpp"
#include "../../Compositor.hpp"

void CInputManager::onTouchDown(wlr_touch_down_event* e) {
    Debug::log(LOG, "Touch screen is pressed down, touch_id: {}", e->touch_id);
    auto       PMONITOR = g_pCompositor->getMonitorFromName(e->touch->output_name ? e->touch->output_name : "");

    
    // if (e->touch_id == 2) {
    //     if (m_sTouchData.touchFocusSurface) {
    //         wlr_seat_touch_notify_up(g_pCompositor->m_sSeat.seat, e->time_msec, 0);
    //         wlr_seat_touch_notify_up(g_pCompositor->m_sSeat.seat, e->time_msec, 1);
    //     }
    // }

    if (e->touch_id == 0) {
        g_pKeybindManager->m_bIsMouseBindActive = true;

        const auto mouseCoords = g_pInputManager->getMouseCoordsInternal();
        CWindow*   pWindow     = g_pCompositor->vectorToWindowIdeal(mouseCoords);

        if (pWindow && !pWindow->m_bIsFullscreen && !pWindow->hasPopupAt(mouseCoords) && pWindow->m_sGroupData.pNextWindow) {
            const wlr_box box = pWindow->getDecorationByType(DECORATION_GROUPBAR)->getWindowDecorationRegion().getExtents();
            if (wlr_box_contains_point(&box, mouseCoords.x, mouseCoords.y)) {
                const int SIZE = pWindow->getGroupSize();
                pWindow        = pWindow->getGroupWindowByIndex((mouseCoords.x - box.x) * SIZE / box.width);

                // hack
                g_pLayoutManager->getCurrentLayout()->onWindowRemoved(pWindow);
                if (!pWindow->m_bIsFloating) {
                    const bool GROUPSLOCKEDPREV        = g_pKeybindManager->m_bGroupsLocked;
                    g_pKeybindManager->m_bGroupsLocked = true;
                    g_pLayoutManager->getCurrentLayout()->onWindowCreated(pWindow);
                    g_pKeybindManager->m_bGroupsLocked = GROUPSLOCKEDPREV;
                }
            }
        }

        g_pInputManager->currentlyDraggedWindow = pWindow;
        g_pInputManager->dragMode               = MBIND_MOVE;
        g_pLayoutManager->getCurrentLayout()->onBeginDragWindow();
        // return;
    }

    const auto PDEVIT = std::find_if(m_lTouchDevices.begin(), m_lTouchDevices.end(), [&](const STouchDevice& other) { return other.pWlrDevice == &e->touch->base; });

    if (PDEVIT != m_lTouchDevices.end() && !PDEVIT->boundOutput.empty())
        PMONITOR = g_pCompositor->getMonitorFromName(PDEVIT->boundOutput);

    PMONITOR = PMONITOR ? PMONITOR : g_pCompositor->m_pLastMonitor;

    wlr_cursor_warp(g_pCompositor->m_sWLRCursor, nullptr, PMONITOR->vecPosition.x + e->x * PMONITOR->vecSize.x, PMONITOR->vecPosition.y + e->y * PMONITOR->vecSize.y);

    refocus();

    if (m_ecbClickBehavior == CLICKMODE_KILL) {
        wlr_pointer_button_event e;
        e.state = WLR_BUTTON_PRESSED;
        g_pInputManager->processMouseDownKill(&e);
        return;
    }

    m_bLastInputTouch = true;

    m_sTouchData.touchFocusWindow  = m_pFoundWindowToFocus;
    m_sTouchData.touchFocusSurface = m_pFoundSurfaceToFocus;
    m_sTouchData.touchFocusLS      = m_pFoundLSToFocus;

    Vector2D local;

    if (m_sTouchData.touchFocusWindow) {
        if (m_sTouchData.touchFocusWindow->m_bIsX11) {
            local = g_pInputManager->getMouseCoordsInternal() - m_sTouchData.touchFocusWindow->m_vRealPosition.goalv();
        } else {
            g_pCompositor->vectorWindowToSurface(g_pInputManager->getMouseCoordsInternal(), m_sTouchData.touchFocusWindow, local);
        }

        m_sTouchData.touchSurfaceOrigin = g_pInputManager->getMouseCoordsInternal() - local;
    } else if (m_sTouchData.touchFocusLS) {
        local = g_pInputManager->getMouseCoordsInternal() - Vector2D(m_sTouchData.touchFocusLS->geometry.x, m_sTouchData.touchFocusLS->geometry.y);

        m_sTouchData.touchSurfaceOrigin = g_pInputManager->getMouseCoordsInternal() - local;
    } else {
        return; // oops, nothing found.
    }

    wlr_seat_touch_notify_down(g_pCompositor->m_sSeat.seat, m_sTouchData.touchFocusSurface, e->time_msec, e->touch_id, local.x, local.y);

    g_pCompositor->notifyIdleActivity();
}

void CInputManager::onTouchUp(wlr_touch_up_event* e) {
    Debug::log(LOG, "Touch screen press is released touch_id: {}", e->touch_id);

    if (e->touch_id == 0) {
        g_pKeybindManager->m_bIsMouseBindActive = false;

        if (g_pInputManager->currentlyDraggedWindow) {
            g_pLayoutManager->getCurrentLayout()->onEndDragWindow();
            g_pInputManager->currentlyDraggedWindow = nullptr;
            g_pInputManager->dragMode               = MBIND_INVALID;
        }
        return;
    }

    // bool found = false;
    //     found = handleKeybinds(MODS, "touchscreen:" + std::to_string(e->touch_id), 0, 0, true, 0);
    //
        // if (found)
        //     shadowKeybinds();

    if (m_sTouchData.touchFocusSurface) {
        wlr_seat_touch_notify_up(g_pCompositor->m_sSeat.seat, e->time_msec, e->touch_id);
    }
}

void CInputManager::onTouchMove(wlr_touch_motion_event* e) {
    Debug::log(LOG, "Touch screen touch is moved x:{}, y:{}", e->x, e->y);
    
    if (e->touch_id == 0) {
        g_pLayoutManager->getCurrentLayout()->onMouseMove(getMouseCoordsInternal());
        // return;
    }
    // if (g_pInputManager->dragMode == MBIND_MOVE)
    //     return;

    if (m_sTouchData.touchFocusWindow && g_pCompositor->windowValidMapped(m_sTouchData.touchFocusWindow)) {
        const auto PMONITOR = g_pCompositor->getMonitorFromID(m_sTouchData.touchFocusWindow->m_iMonitorID);

        // if (e->touch_id == 0) {
            wlr_cursor_warp(g_pCompositor->m_sWLRCursor, nullptr, PMONITOR->vecPosition.x + e->x * PMONITOR->vecSize.x, PMONITOR->vecPosition.y + e->y * PMONITOR->vecSize.y);
        // }

        const auto local = g_pInputManager->getMouseCoordsInternal() - m_sTouchData.touchSurfaceOrigin;

        wlr_seat_touch_notify_motion(g_pCompositor->m_sSeat.seat, e->time_msec, e->touch_id, local.x, local.y);
        wlr_seat_pointer_notify_motion(g_pCompositor->m_sSeat.seat, e->time_msec, local.x, local.y);
    } else if (m_sTouchData.touchFocusLS) {
        const auto PMONITOR = g_pCompositor->getMonitorFromID(m_sTouchData.touchFocusLS->monitorID);

        // if (e->touch_id == 0) {
            wlr_cursor_warp(g_pCompositor->m_sWLRCursor, nullptr, PMONITOR->vecPosition.x + e->x * PMONITOR->vecSize.x, PMONITOR->vecPosition.y + e->y * PMONITOR->vecSize.y);
        // }

        const auto local = g_pInputManager->getMouseCoordsInternal() - m_sTouchData.touchSurfaceOrigin;

        wlr_seat_touch_notify_motion(g_pCompositor->m_sSeat.seat, e->time_msec, e->touch_id, local.x, local.y);
        wlr_seat_pointer_notify_motion(g_pCompositor->m_sSeat.seat, e->time_msec, local.x, local.y);
    }
}

void CInputManager::onPointerHoldBegin(wlr_pointer_hold_begin_event* e) {
    wlr_pointer_gestures_v1_send_hold_begin(g_pCompositor->m_sWLRPointerGestures, g_pCompositor->m_sSeat.seat, e->time_msec, e->fingers);
}

void CInputManager::onPointerHoldEnd(wlr_pointer_hold_end_event* e) {
    wlr_pointer_gestures_v1_send_hold_end(g_pCompositor->m_sWLRPointerGestures, g_pCompositor->m_sSeat.seat, e->time_msec, e->cancelled);
}
