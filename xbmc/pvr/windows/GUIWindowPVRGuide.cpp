/*
 *      Copyright (C) 2012-2013 Team XBMC
 *      http://kodi.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <iterator>

#include "GUIWindowPVRGuide.h"

#include "ContextMenuManager.h"
#include "GUIUserMessages.h"
#include "ServiceBroker.h"
#include "dialogs/GUIDialogBusy.h"
#include "dialogs/GUIDialogNumeric.h"
#include "input/Key.h"
#include "messaging/ApplicationMessenger.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "view/GUIViewState.h"

#include "pvr/PVRGUIActions.h"
#include "pvr/PVRManager.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "pvr/epg/EpgContainer.h"
#include "pvr/windows/GUIEPGGridContainer.h"

using namespace PVR;

CGUIWindowPVRGuideBase::CGUIWindowPVRGuideBase(bool bRadio, int id, const std::string &xmlFile) :
  CGUIWindowPVRBase(bRadio, id, xmlFile),
  m_bChannelSelectionRestored(false)
{
  m_bRefreshTimelineItems = false;
  CServiceBroker::GetPVRManager().EpgContainer().RegisterObserver(this);
}

CGUIWindowPVRGuideBase::~CGUIWindowPVRGuideBase()
{
  CServiceBroker::GetPVRManager().EpgContainer().UnregisterObserver(this);

  m_bRefreshTimelineItems = false;
  StopRefreshTimelineItemsThread();
}

CGUIEPGGridContainer* CGUIWindowPVRGuideBase::GetGridControl()
{
  return dynamic_cast<CGUIEPGGridContainer*>(GetControl(m_viewControl.GetCurrentControl()));
}

void CGUIWindowPVRGuideBase::InitEpgGridControl()
{
  CGUIEPGGridContainer *epgGridContainer = GetGridControl();
  if (epgGridContainer)
  {
    m_bChannelSelectionRestored = epgGridContainer->SetChannel(CServiceBroker::GetPVRManager().GUIActions()->GetSelectedItemPath(m_bRadio));
    epgGridContainer->GoToNow();
  }

  if (epgGridContainer && !epgGridContainer->HasData())
  {
    CSingleLock lock(m_critSection);
    m_bRefreshTimelineItems = true; // force data update on first window open
  }

  StartRefreshTimelineItemsThread();
}

void CGUIWindowPVRGuideBase::ClearData()
{
  {
    CSingleLock lock(m_critSection);
    m_cachedChannelGroup.reset();
    m_newTimeline.reset();
  }

  CGUIWindowPVRBase::ClearData();
}

void CGUIWindowPVRGuideBase::OnInitWindow()
{
  if (m_guiState.get())
    m_viewControl.SetCurrentView(m_guiState->GetViewAsControl(), false);

  if (InitChannelGroup()) // no channels -> lazy init
    InitEpgGridControl();

  CGUIWindowPVRBase::OnInitWindow();
}

void CGUIWindowPVRGuideBase::OnDeinitWindow(int nextWindowID)
{
  StopRefreshTimelineItemsThread();

  m_bChannelSelectionRestored = false;

  {
    CSingleLock lock(m_critSection);
    if (m_vecItems && !m_newTimeline)
    {
      // speedup: save a copy of current items for reuse when re-opening the window
      m_newTimeline.reset(new CFileItemList);
      m_newTimeline->Copy(*m_vecItems);
    }
  }

  CGUIWindowPVRBase::OnDeinitWindow(nextWindowID);
}

void CGUIWindowPVRGuideBase::StartRefreshTimelineItemsThread()
{
  StopRefreshTimelineItemsThread();
  m_refreshTimelineItemsThread.reset(new CPVRRefreshTimelineItemsThread(this));
  m_refreshTimelineItemsThread->Create();
}

void CGUIWindowPVRGuideBase::StopRefreshTimelineItemsThread()
{
  if (m_refreshTimelineItemsThread)
    m_refreshTimelineItemsThread->Stop();
}

void CGUIWindowPVRGuideBase::Notify(const Observable &obs, const ObservableMessage msg)
{
  if (msg == ObservableMessageEpg ||
      msg == ObservableMessageEpgContainer ||
      msg == ObservableMessageChannelGroupReset ||
      msg == ObservableMessageChannelGroup)
  {
    CSingleLock lock(m_critSection);
    m_bRefreshTimelineItems = true;
  }
  else
  {
    CGUIWindowPVRBase::Notify(obs, msg);
  }
}

void CGUIWindowPVRGuideBase::SetInvalid()
{
  CGUIEPGGridContainer *epgGridContainer = GetGridControl();
  if (epgGridContainer)
    epgGridContainer->SetInvalid();

  CGUIWindowPVRBase::SetInvalid();
}

void CGUIWindowPVRGuideBase::GetContextButtons(int itemNumber, CContextButtons &buttons)
{
  buttons.Add(CONTEXT_BUTTON_BEGIN, 19063); /* Go to begin */
  buttons.Add(CONTEXT_BUTTON_NOW,   19070); /* Go to now */
  buttons.Add(CONTEXT_BUTTON_DATE,  19288); /* Go to date */
  buttons.Add(CONTEXT_BUTTON_END,   19064); /* Go to end */

  CGUIWindowPVRBase::GetContextButtons(itemNumber, buttons);
}

void CGUIWindowPVRGuideBase::UpdateSelectedItemPath()
{
  CGUIEPGGridContainer *epgGridContainer = GetGridControl();
  if (epgGridContainer)
  {
    CPVRChannelPtr channel(epgGridContainer->GetSelectedChannel());
    if (channel)
      CServiceBroker::GetPVRManager().GUIActions()->SetSelectedItemPath(m_bRadio, channel->Path());
  }
}

void CGUIWindowPVRGuideBase::UpdateButtons(void)
{
  CGUIWindowPVRBase::UpdateButtons();

  SET_CONTROL_LABEL(CONTROL_LABEL_HEADER1, g_localizeStrings.Get(19032));
  SET_CONTROL_LABEL(CONTROL_LABEL_HEADER2, GetChannelGroup()->GroupName());
}

bool CGUIWindowPVRGuideBase::Update(const std::string &strDirectory, bool updateFilterPath /* = true */)
{
  bool bReturn = CGUIWindowPVRBase::Update(strDirectory, updateFilterPath);

  if (bReturn && !m_bChannelSelectionRestored)
  {
    CGUIEPGGridContainer* epgGridContainer = GetGridControl();
    if (epgGridContainer)
      m_bChannelSelectionRestored = epgGridContainer->SetChannel(CServiceBroker::GetPVRManager().GUIActions()->GetSelectedItemPath(m_bRadio));
  }

  return bReturn;
}

bool CGUIWindowPVRGuideBase::GetDirectory(const std::string &strDirectory, CFileItemList &items)
{
  bool bRefreshTimelineItems = false;

  {
    CSingleLock lock(m_critSection);

    if (m_cachedChannelGroup && *m_cachedChannelGroup != *GetChannelGroup())
    {
      // channel group change and not very first open of this window. force immediate update.
      m_bRefreshTimelineItems = true;
      bRefreshTimelineItems = true;
    }
  }

  // never call DoRefresh with locked mutex!
  if (bRefreshTimelineItems)
    m_refreshTimelineItemsThread->DoRefresh();

  {
    CSingleLock lock(m_critSection);

    // Note: no need to do anything if no new data available. items always contains previous data.
    if (m_newTimeline)
    {
      items.RemoveDiscCache(GetID());
      items.Assign(*m_newTimeline, false);
      m_newTimeline.reset();
    }
  }

  return true;
}

bool CGUIWindowPVRGuideBase::ShouldNavigateToGridContainer(int iAction)
{
  CGUIEPGGridContainer *epgGridContainer = GetGridControl();
  CGUIControl* control = GetControl(CONTROL_LSTCHANNELGROUPS);
  if (epgGridContainer && control &&
      GetFocusedControlID() == control->GetID())
  {
    int iNavigationId = control->GetAction(iAction).GetNavigation();
    if (iNavigationId > 0)
    {
      control = epgGridContainer;
      while (control != this) // navigation target could be the grid control or one of its parent controls.
      {
        if (iNavigationId == control->GetID())
        {
          // channel group selector control's target for the action is the grid control
          return true;
        }
        control = control->GetParentControl();
      }
    }
  }
  return false;
}

bool CGUIWindowPVRGuideBase::OnAction(const CAction &action)
{
  switch (action.GetID())
  {
    case ACTION_MOVE_UP:
    case ACTION_MOVE_DOWN:
    case ACTION_MOVE_LEFT:
    case ACTION_MOVE_RIGHT:
    {
      // Check whether grid container is configured as channel group selector's navigation target for the given action.
      if (ShouldNavigateToGridContainer(action.GetID()))
      {
        CGUIEPGGridContainer *epgGridContainer = GetGridControl();
        if (epgGridContainer)
        {
          CGUIWindowPVRBase::OnAction(action);

          switch (action.GetID())
          {
            case ACTION_MOVE_UP:
              epgGridContainer->GoToBottom();
              return true;
            case ACTION_MOVE_DOWN:
              epgGridContainer->GoToTop();
              return true;
            case ACTION_MOVE_LEFT:
              epgGridContainer->GoToMostRight();
              return true;
            case ACTION_MOVE_RIGHT:
              epgGridContainer->GoToMostLeft();
              return true;
            default:
              break;
          }
        }
      }
      break;
    }
    case REMOTE_0:
      if (GetCurrentDigitCount() == 0)
      {
        // single zero input is handled by epg grid container
        break;
      }
      // fall-thru is intended
    case REMOTE_1:
    case REMOTE_2:
    case REMOTE_3:
    case REMOTE_4:
    case REMOTE_5:
    case REMOTE_6:
    case REMOTE_7:
    case REMOTE_8:
    case REMOTE_9:
      AppendChannelNumberCharacter((action.GetID() - REMOTE_0) + '0');
      return true;

    case ACTION_CHANNEL_NUMBER_SEP:
      AppendChannelNumberCharacter(CPVRChannelNumber::SEPARATOR);
      return true;
  }

  return CGUIWindowPVRBase::OnAction(action);
}

bool CGUIWindowPVRGuideBase::OnMessage(CGUIMessage& message)
{
  bool bReturn = false;
  switch (message.GetMessage())
  {
    case GUI_MSG_CLICKED:
    {
      if (message.GetSenderId() == m_viewControl.GetCurrentControl())
      {
        int iItem = m_viewControl.GetSelectedItem();
        if (iItem >= 0 && iItem < m_vecItems->Size())
        {
          CFileItemPtr pItem = m_vecItems->Get(iItem);
          /* process actions */
          switch (message.GetParam1())
          {
            case ACTION_SELECT_ITEM:
            case ACTION_MOUSE_LEFT_CLICK:
              switch(CServiceBroker::GetSettings().GetInt(CSettings::SETTING_EPG_SELECTACTION))
              {
                case EPG_SELECT_ACTION_CONTEXT_MENU:
                  OnPopupMenu(iItem);
                  bReturn = true;
                  break;
                case EPG_SELECT_ACTION_SWITCH:
                  CServiceBroker::GetPVRManager().GUIActions()->SwitchToChannel(pItem, true);
                  bReturn = true;
                  break;
                case EPG_SELECT_ACTION_PLAY_RECORDING:
                  CServiceBroker::GetPVRManager().GUIActions()->PlayRecording(pItem, true);
                  bReturn = true;
                  break;
                case EPG_SELECT_ACTION_INFO:
                  CServiceBroker::GetPVRManager().GUIActions()->ShowEPGInfo(pItem);
                  bReturn = true;
                  break;
                case EPG_SELECT_ACTION_RECORD:
                  CServiceBroker::GetPVRManager().GUIActions()->ToggleTimer(pItem);
                  bReturn = true;
                  break;
                case EPG_SELECT_ACTION_SMART_SELECT:
                {
                  const CPVREpgInfoTagPtr tag(pItem->GetEPGInfoTag());
                  if (tag)
                  {
                    const CDateTime start(tag->StartAsUTC());
                    const CDateTime end(tag->EndAsUTC());
                    const CDateTime now(CDateTime::GetUTCDateTime());

                    if (start <= now && now <= end)
                    {
                      // current event
                      CServiceBroker::GetPVRManager().GUIActions()->SwitchToChannel(pItem, true);
                    }
                    else if (now < start)
                    {
                      // future event
                      if (tag->HasTimer())
                        CServiceBroker::GetPVRManager().GUIActions()->EditTimer(pItem);
                      else
                        CServiceBroker::GetPVRManager().GUIActions()->AddTimer(pItem, false);
                    }
                    else
                    {
                      // past event
                      if (tag->HasRecording())
                        CServiceBroker::GetPVRManager().GUIActions()->PlayRecording(pItem, true);
                      else if (tag->IsPlayable())
                        CServiceBroker::GetPVRManager().GUIActions()->PlayEpgTag(pItem);
                      else
                        CServiceBroker::GetPVRManager().GUIActions()->ShowEPGInfo(pItem);
                    }
                    bReturn = true;
                  }
                  break;
                }
              }
              break;
            case ACTION_SHOW_INFO:
              CServiceBroker::GetPVRManager().GUIActions()->ShowEPGInfo(pItem);
              bReturn = true;
              break;
            case ACTION_PLAYER_PLAY:
              CServiceBroker::GetPVRManager().GUIActions()->SwitchToChannel(pItem, true);
              bReturn = true;
              break;
            case ACTION_RECORD:
              CServiceBroker::GetPVRManager().GUIActions()->ToggleTimer(pItem);
              bReturn = true;
              break;
            case ACTION_PVR_SHOW_TIMER_RULE:
              CServiceBroker::GetPVRManager().GUIActions()->AddTimerRule(pItem, true);
              bReturn = true;
              break;
            case ACTION_CONTEXT_MENU:
            case ACTION_MOUSE_RIGHT_CLICK:
              OnPopupMenu(iItem);
              bReturn = true;
              break;
          }
        }
        else if (iItem == -1)
        {
          /* process actions */
          switch (message.GetParam1())
          {
            case ACTION_SELECT_ITEM:
            case ACTION_MOUSE_LEFT_CLICK:
            case ACTION_PLAYER_PLAY:
            {
              // EPG "gap" selected => switch to associated channel.
              CGUIEPGGridContainer *epgGridContainer = GetGridControl();
              if (epgGridContainer)
              {
                const CFileItemPtr item(epgGridContainer->GetSelectedChannelItem());
                if (item)
                {
                  CServiceBroker::GetPVRManager().GUIActions()->SwitchToChannel(item, true);
                  bReturn = true;
                }
              }
              break;
            }
            case ACTION_CONTEXT_MENU:
            {
              // EPG "gap" selected => create and process special context menu with item independent entries.
              CContextButtons buttons;
              GetContextButtons(-1, buttons);

              int iButton = CGUIDialogContextMenu::ShowAndGetChoice(buttons);
              if (iButton >= 0)
              {
                bReturn = OnContextButton(-1, static_cast<CONTEXT_BUTTON>(iButton));
              }
              break;
            }
          }
        }
      }
      else if (message.GetSenderId() == CONTROL_BTNVIEWASICONS)
      {
        // let's set the view mode first before update
        CGUIWindowPVRBase::OnMessage(message);
        Refresh(true);
        bReturn = true;
      }
      break;
    }
    case GUI_MSG_CHANGE_VIEW_MODE:
    {
      // let's set the view mode first before update
      CGUIWindowPVRBase::OnMessage(message);

      // force data update for the new view control
      {
        CSingleLock lock(m_critSection);
        m_bRefreshTimelineItems = true;
      }
      InitEpgGridControl();

      Refresh(true);
      bReturn = true;
      break;
    }
    case GUI_MSG_REFRESH_LIST:
      switch(message.GetParam1())
      {
        case ObservableMessageChannelGroupsLoaded:
        {
          // late init
          InitChannelGroup();
          InitEpgGridControl();
          break;
        }
        case ObservableMessageChannelGroupReset:
        case ObservableMessageChannelGroup:
        case ObservableMessageEpg:
        case ObservableMessageEpgContainer:
        {
          Refresh(true);
          break;
        }
        case ObservableMessageTimersReset:
        case ObservableMessageTimers:
        {
          SetInvalid();
          break;
        }
      }
      break;
  }

  return bReturn || CGUIWindowPVRBase::OnMessage(message);
}

bool CGUIWindowPVRGuideBase::OnContextButton(int itemNumber, CONTEXT_BUTTON button)
{
  switch (button)
  {
    case CONTEXT_BUTTON_BEGIN:
      return OnContextButtonBegin();

    case CONTEXT_BUTTON_NOW:
      return OnContextButtonNow();

    case CONTEXT_BUTTON_DATE:
      return OnContextButtonDate();

    case CONTEXT_BUTTON_END:
      return OnContextButtonEnd();

    default:
      break;
  }

  if (itemNumber < 0 || itemNumber >= m_vecItems->Size())
    return false;

  return CGUIMediaWindow::OnContextButton(itemNumber, button);
}

bool CGUIWindowPVRGuideBase::RefreshTimelineItems()
{
  bool bRefreshTimelineItems;
  {
    CSingleLock lock(m_critSection);

    bRefreshTimelineItems = m_bRefreshTimelineItems;
    m_bRefreshTimelineItems = false;
  }

  if (bRefreshTimelineItems)
  {
    CGUIEPGGridContainer* epgGridContainer = GetGridControl();
    if (epgGridContainer)
    {
      const CPVRChannelGroupPtr group(GetChannelGroup());
      if (!group)
        return false;

      std::unique_ptr<CFileItemList> timeline(new CFileItemList);

      // can be very expensive. never call with lock acquired.
      group->GetEPGAll(*timeline, true);

      CDateTime startDate(group->GetFirstEPGDate());
      CDateTime endDate(group->GetLastEPGDate());
      const CDateTime currentDate(CDateTime::GetCurrentDateTime().GetAsUTCDateTime());

      if (!startDate.IsValid())
        startDate = currentDate;

      if (!endDate.IsValid() || endDate < startDate)
        endDate = startDate;

      // limit start to linger time
      int iPastDays = CServiceBroker::GetPVRManager().EpgContainer().GetPastDaysToDisplay();
      const CDateTime maxPastDate(currentDate - CDateTimeSpan(iPastDays, 0, 0, 0));
      if (startDate < maxPastDate)
        startDate = maxPastDate;

      // can be very expensive. never call with lock acquired.
      epgGridContainer->SetTimelineItems(timeline, startDate, endDate);

      {
        CSingleLock lock(m_critSection);

        m_newTimeline = std::move(timeline);
        m_cachedChannelGroup = group;
      }
      return true;
    }
  }
  return false;
}

bool CGUIWindowPVRGuideBase::OnContextButtonBegin()
{
  GetGridControl()->GoToBegin();
  return true;
}

bool CGUIWindowPVRGuideBase::OnContextButtonEnd()
{
  GetGridControl()->GoToEnd();
  return true;
}

bool CGUIWindowPVRGuideBase::OnContextButtonNow()
{
  GetGridControl()->GoToNow();
  return true;
}

bool CGUIWindowPVRGuideBase::OnContextButtonDate()
{
  bool bReturn = false;

  SYSTEMTIME date;
  CGUIEPGGridContainer* epgGridContainer = GetGridControl();
  epgGridContainer->GetSelectedDate().GetAsSystemTime(date);

  if (CGUIDialogNumeric::ShowAndGetDate(date, g_localizeStrings.Get(19288))) /* Go to date */
  {
    epgGridContainer->GoToDate(CDateTime(date));
    bReturn = true;
  }

  return bReturn;
}

void CGUIWindowPVRGuideBase::OnInputDone()
{
  const CPVRChannelNumber channelNumber = GetChannelNumber();
  if (channelNumber.IsValid())
  {
    for (const CFileItemPtr event : m_vecItems->GetList())
    {
      const CPVREpgInfoTagPtr tag(event->GetEPGInfoTag());
      if (tag->HasChannel() && tag->Channel()->ChannelNumber() == channelNumber)
      {
        CGUIEPGGridContainer* epgGridContainer = GetGridControl();
        if (epgGridContainer)
        {
          epgGridContainer->SetChannel(tag->Channel());
          return;
        }
      }
    }
  }
}

CPVRRefreshTimelineItemsThread::CPVRRefreshTimelineItemsThread(CGUIWindowPVRGuideBase *pGuideWindow)
: CThread("epg-grid-refresh-timeline-items"),
  m_pGuideWindow(pGuideWindow),
  m_ready(true),
  m_done(false)
{
}

CPVRRefreshTimelineItemsThread::~CPVRRefreshTimelineItemsThread()
{
  // Note: CThread dtor will also call StopThread(true), but if thread worker function exits that
  //       late, it might access member variables of this which are already destroyed. Thus, stop
  //       the thread worker here and synchronously, while all members of this are still alive.
  StopThread(true);
}

void CPVRRefreshTimelineItemsThread::Stop()
{
  StopThread(false);
  m_ready.Set(); // wake up the worker thread to let it exit
}

void CPVRRefreshTimelineItemsThread::DoRefresh()
{
  m_ready.Set(); // wake up the worker thread
  m_done.Reset();
  CGUIDialogBusy::WaitOnEvent(m_done, 100, false);
}

void CPVRRefreshTimelineItemsThread::Process()
{
  static const int BOOSTED_SLEEPS_THRESHOLD = 4;

  int iLastEpgItemsCount = 0;
  int iUpdatesWithoutChange = 0;

  while (!m_bStop)
  {
    m_done.Reset();

    if (m_pGuideWindow->RefreshTimelineItems() && !m_bStop)
    {
      CGUIMessage m(GUI_MSG_REFRESH_LIST, m_pGuideWindow->GetID(), 0, ObservableMessageEpg);
      KODI::MESSAGING::CApplicationMessenger::GetInstance().SendGUIMessage(m);
    }

    if (m_bStop)
      break;

    m_done.Set();

    // in order to fill the guide window asap, use a short update interval until we the
    // same amount of epg events for BOOSTED_SLEEPS_THRESHOLD + 1 times in a row .
    if (iUpdatesWithoutChange < BOOSTED_SLEEPS_THRESHOLD)
    {
      int iCurrentEpgItemsCount = m_pGuideWindow->CurrentDirectory().Size();

      if (iCurrentEpgItemsCount == iLastEpgItemsCount)
        iUpdatesWithoutChange++;
      else
        iUpdatesWithoutChange = 0; // reset

      iLastEpgItemsCount = iCurrentEpgItemsCount;

      m_ready.WaitMSec(1000); // boosted update cycle
    }
    else
    {
      m_ready.WaitMSec(5000); // normal update cycle
    }

    m_ready.Reset();
  }

  m_ready.Reset();
  m_done.Set();
}
