// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file contains tests for extension loading, reloading, and
// unloading behavior.

#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/version.h"
#include "base/win/windows_version.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/devtools_util.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/test_extension_dir.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/process_manager.h"
#include "extensions/test/extension_test_message_listener.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace extensions {
namespace {

class ExtensionLoadingTest : public ExtensionBrowserTest {
};

// Check the fix for http://crbug.com/178542.
IN_PROC_BROWSER_TEST_F(ExtensionLoadingTest,
                       UpgradeAfterNavigatingFromOverriddenNewTabPage) {
  embedded_test_server()->ServeFilesFromDirectory(
      base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(embedded_test_server()->Start());

  TestExtensionDir extension_dir;
  const char manifest_template[] =
      "{"
      "  'name': 'Overrides New Tab',"
      "  'version': '%d',"
      "  'description': 'Overrides New Tab',"
      "  'manifest_version': 2,"
      "  'background': {"
      "    'persistent': false,"
      "    'scripts': ['event.js']"
      "  },"
      "  'chrome_url_overrides': {"
      "    'newtab': 'newtab.html'"
      "  }"
      "}";
  extension_dir.WriteManifestWithSingleQuotes(
      base::StringPrintf(manifest_template, 1));
  extension_dir.WriteFile(FILE_PATH_LITERAL("event.js"), "");
  extension_dir.WriteFile(FILE_PATH_LITERAL("newtab.html"),
                          "<h1>Overridden New Tab Page</h1>");

  const Extension* new_tab_extension =
      InstallExtension(extension_dir.Pack(), 1 /*new install*/);
  ASSERT_TRUE(new_tab_extension);

  // Visit the New Tab Page to get a renderer using the extension into history.
  ui_test_utils::NavigateToURL(browser(), GURL("chrome://newtab"));

  // Navigate that tab to a non-extension URL to swap out the extension's
  // renderer.
  const GURL test_link_from_NTP =
      embedded_test_server()->GetURL("/README.chromium");
  EXPECT_THAT(test_link_from_NTP.spec(), testing::EndsWith("/README.chromium"))
      << "Check that the test server started.";
  NavigateInRenderer(browser()->tab_strip_model()->GetActiveWebContents(),
                     test_link_from_NTP);

  // Increase the extension's version.
  extension_dir.WriteManifestWithSingleQuotes(
      base::StringPrintf(manifest_template, 2));

  // Upgrade the extension.
  new_tab_extension = UpdateExtension(
      new_tab_extension->id(), extension_dir.Pack(), 0 /*expected upgrade*/);
  EXPECT_THAT(new_tab_extension->version()->components(),
              testing::ElementsAre(2));

  // The extension takes a couple round-trips to the renderer in order
  // to crash, so open a new tab to wait long enough.
  AddTabAtIndex(browser()->tab_strip_model()->count(),
                GURL("http://www.google.com/"),
                ui::PAGE_TRANSITION_TYPED);

  // Check that the extension hasn't crashed.
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  EXPECT_EQ(0U, registry->terminated_extensions().size());
  EXPECT_TRUE(registry->enabled_extensions().Contains(new_tab_extension->id()));
}

// Tests the behavior described in http://crbug.com/532088.
IN_PROC_BROWSER_TEST_F(ExtensionLoadingTest,
                       KeepAliveWithDevToolsOpenOnReload) {
#if defined(OS_WIN)
  // Flaky on Win XP SP3. http://crbug.com/560716.
  if (base::win::GetVersion() <= base::win::VERSION_SERVER_2003)
    return;
#endif

  embedded_test_server()->ServeFilesFromDirectory(
      base::FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(embedded_test_server()->Start());

  TestExtensionDir extension_dir;
  const char manifest_contents[] =
      "{"
      "  'name': 'Test With Lazy Background Page',"
      "  'version': '0',"
      "  'manifest_version': 2,"
      "  'app': {"
      "    'background': {"
      "       'scripts': ['event.js']"
      "    }"
      "  }"
      "}";
  extension_dir.WriteManifestWithSingleQuotes(manifest_contents);
  extension_dir.WriteFile(FILE_PATH_LITERAL("event.js"), "");

  const Extension* extension =
      InstallExtension(extension_dir.Pack(), 1 /*new install*/);
  ASSERT_TRUE(extension);
  std::string extension_id = extension->id();

  ProcessManager* process_manager = ProcessManager::Get(profile());
  EXPECT_EQ(0, process_manager->GetLazyKeepaliveCount(extension));

  devtools_util::InspectBackgroundPage(extension, profile());
  EXPECT_EQ(1, process_manager->GetLazyKeepaliveCount(extension));

  // Opening DevTools will cause the background page to load. Wait for it.
  WaitForExtensionViewsToLoad();

  ReloadExtension(extension_id);

  // Flush the MessageLoop to ensure that DevTools has a chance to be reattached
  // and the background page has a chance to begin reloading.
  base::RunLoop().RunUntilIdle();

  // And wait for the background page to finish loading again.
  WaitForExtensionViewsToLoad();

  // Ensure that our DevtoolsAgentHost is actually connected to the new
  // background WebContents.
  content::WebContents* background_contents =
      process_manager->GetBackgroundHostForExtension(extension_id)
          ->host_contents();
  EXPECT_TRUE(content::DevToolsAgentHost::HasFor(background_contents));

  // The old Extension object is no longer valid.
  extension = ExtensionRegistry::Get(profile())
      ->enabled_extensions().GetByID(extension_id);

  // Keepalive count should stabilize back to 1, because DevTools is still open.
  EXPECT_EQ(1, process_manager->GetLazyKeepaliveCount(extension));
}

// Tests whether the extension runtime stays valid when an extension reloads
// while a devtools extension is hammering the frame with eval requests.
// Regression test for https://crbug.com/544182
IN_PROC_BROWSER_TEST_F(ExtensionLoadingTest, RuntimeValidWhileDevToolsOpen) {
  TestExtensionDir devtools_dir;
  TestExtensionDir inspect_dir;

  const char kDevtoolsManifest[] =
      "{"
      "  'name': 'Devtools',"
      "  'version': '1',"
      "  'manifest_version': 2,"
      "  'devtools_page': 'devtools.html'"
      "}";

  const char kDevtoolsJs[] =
      "setInterval(function() {"
      "  chrome.devtools.inspectedWindow.eval('1', function() {"
      "  });"
      "}, 4);"
      "chrome.test.sendMessage('devtools_page_ready');";

  const char kTargetManifest[] =
      "{"
      "  'name': 'Inspect target',"
      "  'version': '1',"
      "  'manifest_version': 2,"
      "  'background': {"
      "    'scripts': ['background.js']"
      "  }"
      "}";

  // A script to duck-type whether it runs in a background page.
  const char kTargetJs[] =
      "var is_valid = !!(chrome.tabs && chrome.tabs.create);";

  devtools_dir.WriteManifestWithSingleQuotes(kDevtoolsManifest);
  devtools_dir.WriteFile(FILE_PATH_LITERAL("devtools.js"), kDevtoolsJs);
  devtools_dir.WriteFile(FILE_PATH_LITERAL("devtools.html"),
                         "<script src='devtools.js'></script>");

  inspect_dir.WriteManifestWithSingleQuotes(kTargetManifest);
  inspect_dir.WriteFile(FILE_PATH_LITERAL("background.js"), kTargetJs);
  const Extension* devtools_ext = LoadExtension(devtools_dir.unpacked_path());
  ASSERT_TRUE(devtools_ext);

  const Extension* inspect_ext = LoadExtension(inspect_dir.unpacked_path());
  ASSERT_TRUE(inspect_ext);
  const std::string inspect_ext_id = inspect_ext->id();

  // Open the devtools and wait until the devtools_page is ready.
  ExtensionTestMessageListener devtools_ready("devtools_page_ready", false);
  devtools_util::InspectBackgroundPage(inspect_ext, profile());
  ASSERT_TRUE(devtools_ready.WaitUntilSatisfied());

  // Reload the extension. The devtools window will stay open, but temporarily
  // be detached. As soon as the background is attached again, the devtools
  // continues with spamming eval requests.
  ReloadExtension(inspect_ext_id);
  WaitForExtensionViewsToLoad();

  content::WebContents* bg_contents =
      ProcessManager::Get(profile())
          ->GetBackgroundHostForExtension(inspect_ext_id)
          ->host_contents();
  ASSERT_TRUE(bg_contents);

  // Now check whether the extension runtime is valid (see kTargetJs).
  bool is_valid = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      bg_contents, "domAutomationController.send(is_valid);", &is_valid));
  EXPECT_TRUE(is_valid);
}

}  // namespace
}  // namespace extensions
