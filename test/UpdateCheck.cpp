/*
 * This file is part of Adblock Plus <https://adblockplus.org/>,
 * Copyright (C) 2006-2017 eyeo GmbH
 *
 * Adblock Plus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Adblock Plus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Adblock Plus.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <functional>

#include "BaseJsTest.h"

using namespace AdblockPlus;

namespace
{
  typedef std::shared_ptr<AdblockPlus::FilterEngine> FilterEnginePtr;

  void FindAndReplace(std::string& source, const std::string& find, const std::string& replace)
  {
    for (size_t pos = 0; (pos = source.find(find), pos) != std::string::npos; pos += replace.size())
      source.replace(pos, find.size(), replace);
  }

  class UpdateCheckTest : public ::testing::Test
  {
  protected:
    AdblockPlus::AppInfo appInfo;
    AdblockPlus::ServerResponse webRequestResponse;
    DelayedWebRequest::SharedTasks webRequestTasks;
    DelayedTimer::SharedTasks timerTasks;
    AdblockPlus::JsEnginePtr jsEngine;
    FilterEnginePtr filterEngine;

    bool eventCallbackCalled;
    AdblockPlus::JsValueList eventCallbackParams;
    bool updateCallbackCalled;
    std::string updateError;

    void SetUp()
    {
      eventCallbackCalled = false;
      updateCallbackCalled = false;
      Reset();
    }

    void Reset()
    {
      JsEngineCreationParameters jsEngineParams;
      jsEngineParams.appInfo = appInfo;
      jsEngineParams.logSystem.reset(new LazyLogSystem());
      jsEngineParams.fileSystem.reset(new LazyFileSystem());
      jsEngineParams.timer = DelayedTimer::New(timerTasks);
      jsEngineParams.webRequest = DelayedWebRequest::New(webRequestTasks);
      jsEngine = CreateJsEngine(std::move(jsEngineParams));
      jsEngine->SetEventCallback("updateAvailable", [this](JsValueList&& params)
      {
        eventCallbackCalled = true;
        eventCallbackParams = std::move(params);
      });

      filterEngine = AdblockPlus::FilterEngine::Create(jsEngine);
    }

    // Returns a URL or the empty string if there is no such request.
    std::string ProcessPendingUpdateWebRequest()
    {
      auto ii = webRequestTasks->begin();
      while (ii != webRequestTasks->end())
      {
        if (ii->url.find("update.json") != std::string::npos)
        {
          ii->getCallback(webRequestResponse);
          auto url = ii->url;
          webRequestTasks->erase(ii);
          return url;
        }
        ++ii;
      }
      return std::string();
    }

    void ForceUpdateCheck()
    {
      filterEngine->ForceUpdateCheck([this](const std::string& error)
      {
        updateCallbackCalled = true;
        updateError = error;
      });
      DelayedTimer::ProcessImmediateTimers(timerTasks);
    }
  };
}

TEST_F(UpdateCheckTest, RequestFailure)
{
  webRequestResponse.status = IWebRequest::NS_ERROR_FAILURE;

  appInfo.name = "1";
  appInfo.version = "3";
  appInfo.application = "4";
  appInfo.applicationVersion = "2";
  appInfo.developmentBuild = false;

  Reset();
  ForceUpdateCheck();

  auto requestUrl = ProcessPendingUpdateWebRequest();

  ASSERT_FALSE(eventCallbackCalled);
  ASSERT_TRUE(updateCallbackCalled);
  ASSERT_FALSE(updateError.empty());

  std::string expectedUrl(filterEngine->GetPref("update_url_release").AsString());
  std::string platform = jsEngine->Evaluate("require('info').platform").AsString();
  std::string platformVersion = jsEngine->Evaluate("require('info').platformVersion").AsString();

  FindAndReplace(expectedUrl, "%NAME%", appInfo.name);
  FindAndReplace(expectedUrl, "%TYPE%", "1");   // manual update
  expectedUrl += "&addonName=" + appInfo.name +
                 "&addonVersion=" + appInfo.version +
                 "&application=" + appInfo.application +
                 "&applicationVersion=" + appInfo.applicationVersion +
                 "&platform=" + platform +
                 "&platformVersion=" + platformVersion +
                 "&lastVersion=0&downloadCount=0";
  ASSERT_EQ(expectedUrl, requestUrl);
}

TEST_F(UpdateCheckTest, UpdateAvailable)
{
  webRequestResponse.status = IWebRequest::NS_OK;
  webRequestResponse.responseStatus = 200;
  webRequestResponse.responseText = "{\"1\": {\"version\":\"3.1\",\"url\":\"https://foo.bar/\"}}";

  appInfo.name = "1";
  appInfo.version = "3";
  appInfo.application = "4";
  appInfo.applicationVersion = "2";
  appInfo.developmentBuild = true;

  Reset();
  ForceUpdateCheck();

  auto requestUrl = ProcessPendingUpdateWebRequest();

  ASSERT_TRUE(eventCallbackCalled);
  ASSERT_EQ(1u, eventCallbackParams.size());
  ASSERT_EQ("https://foo.bar/", eventCallbackParams[0].AsString());
  ASSERT_TRUE(updateCallbackCalled);
  ASSERT_TRUE(updateError.empty());

  std::string expectedUrl(filterEngine->GetPref("update_url_devbuild").AsString());
  std::string platform = jsEngine->Evaluate("require('info').platform").AsString();
  std::string platformVersion = jsEngine->Evaluate("require('info').platformVersion").AsString();

  FindAndReplace(expectedUrl, "%NAME%", appInfo.name);
  FindAndReplace(expectedUrl, "%TYPE%", "1");   // manual update
  expectedUrl += "&addonName=" + appInfo.name +
                 "&addonVersion=" + appInfo.version +
                 "&application=" + appInfo.application +
                 "&applicationVersion=" + appInfo.applicationVersion +
                 "&platform=" + platform +
                 "&platformVersion=" + platformVersion +
                 "&lastVersion=0&downloadCount=0";
  ASSERT_EQ(expectedUrl, requestUrl);
}

TEST_F(UpdateCheckTest, ApplicationUpdateAvailable)
{
  webRequestResponse.status = IWebRequest::NS_OK;
  webRequestResponse.responseStatus = 200;
  webRequestResponse.responseText = "{\"1/4\": {\"version\":\"3.1\",\"url\":\"https://foo.bar/\"}}";

  appInfo.name = "1";
  appInfo.version = "3";
  appInfo.application = "4";
  appInfo.applicationVersion = "2";
  appInfo.developmentBuild = true;

  Reset();
  ForceUpdateCheck();

  ProcessPendingUpdateWebRequest();

  ASSERT_TRUE(eventCallbackCalled);
  ASSERT_EQ(1u, eventCallbackParams.size());
  ASSERT_EQ("https://foo.bar/", eventCallbackParams[0].AsString());
  ASSERT_TRUE(updateError.empty());
}

TEST_F(UpdateCheckTest, WrongApplication)
{
  webRequestResponse.status = IWebRequest::NS_OK;
  webRequestResponse.responseStatus = 200;
  webRequestResponse.responseText = "{\"1/3\": {\"version\":\"3.1\",\"url\":\"https://foo.bar/\"}}";

  appInfo.name = "1";
  appInfo.version = "3";
  appInfo.application = "4";
  appInfo.applicationVersion = "2";
  appInfo.developmentBuild = true;

  Reset();
  ForceUpdateCheck();

  ProcessPendingUpdateWebRequest();

  ASSERT_FALSE(eventCallbackCalled);
  ASSERT_TRUE(updateCallbackCalled);
  ASSERT_TRUE(updateError.empty());
}

TEST_F(UpdateCheckTest, WrongVersion)
{
  webRequestResponse.status = IWebRequest::NS_OK;
  webRequestResponse.responseStatus = 200;
  webRequestResponse.responseText = "{\"1\": {\"version\":\"3\",\"url\":\"https://foo.bar/\"}}";

  appInfo.name = "1";
  appInfo.version = "3";
  appInfo.application = "4";
  appInfo.applicationVersion = "2";
  appInfo.developmentBuild = true;

  Reset();
  ForceUpdateCheck();

  ProcessPendingUpdateWebRequest();

  ASSERT_FALSE(eventCallbackCalled);
  ASSERT_TRUE(updateCallbackCalled);
  ASSERT_TRUE(updateError.empty());
}

TEST_F(UpdateCheckTest, WrongURL)
{
  webRequestResponse.status = IWebRequest::NS_OK;
  webRequestResponse.responseStatus = 200;
  webRequestResponse.responseText = "{\"1\": {\"version\":\"3.1\",\"url\":\"http://insecure/\"}}";

  appInfo.name = "1";
  appInfo.version = "3";
  appInfo.application = "4";
  appInfo.applicationVersion = "2";
  appInfo.developmentBuild = true;

  Reset();
  ForceUpdateCheck();

  ProcessPendingUpdateWebRequest();

  ASSERT_FALSE(eventCallbackCalled);
  ASSERT_TRUE(updateCallbackCalled);
  ASSERT_FALSE(updateError.empty());
}
