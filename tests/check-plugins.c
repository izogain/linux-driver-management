/*
 * This file is part of linux-driver-management.
 *
 * Copyright © 2016-2018 Ikey Doherty
 *
 * linux-driver-management is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <umockdev.h>

#include "ldm-private.h"
#include "ldm.h"
#include "util.h"

DEF_AUTOFREE(UMockdevTestbed, g_object_unref)

#define NV_MOCKDEV_FILE TEST_DATA_ROOT "/nvidia1060.umockdev"
#define OPTIMUS_MOCKDEV_FILE TEST_DATA_ROOT "/optimus765m.umockdev"

#define NV_MAIN_MODALIAS TEST_DATA_ROOT "/nvidia-glx-driver.modaliases"
#define NV_340_MODALIAS TEST_DATA_ROOT "/nvidia-340-glx-driver.modaliases"
#define MODALIAS_DIR TEST_DATA_ROOT "/"

#define RAZER_MOCKDEV_FILE TEST_DATA_ROOT "/razer-ornata-chroma.umockdev"
#define RAZER_MODALIAS TEST_DATA_ROOT "razer-drivers.modaliases"

static UMockdevTestbed *create_bed_from(const char *mockdevname)
{
        UMockdevTestbed *bed = NULL;

        bed = umockdev_testbed_new();
        fail_if(!umockdev_testbed_add_from_file(bed, mockdevname, NULL),
                "Failed to create device: %s",
                mockdevname);

        return bed;
}

/**
 * Simplistic test focusing on grabbing the correct driver for a basic
 * single GPU system
 */
START_TEST(test_plugins_nvidia)
{
        g_autoptr(LdmManager) manager = NULL;
        autofree(UMockdevTestbed) *bed = NULL;
        g_autoptr(LdmGPUConfig) gpu = NULL;
        g_autoptr(GPtrArray) providers = NULL;
        const gchar *plugin_id = NULL;

        bed = create_bed_from(NV_MOCKDEV_FILE);
        manager = ldm_manager_new(0);

        fail_if(!ldm_manager_add_modalias_plugin_for_path(manager, NV_MAIN_MODALIAS),
                "Failed to add main modalias file");
        fail_if(!ldm_manager_add_modalias_plugin_for_path(manager, NV_340_MODALIAS),
                "Failed to add 340 modalias file");

        gpu = ldm_gpu_config_new(manager);
        fail_if(!gpu, "Failed to create GPUConfig");

        providers = ldm_gpu_config_get_providers(gpu);
        fail_if(providers->len != 1, "Expected 1 provider, got %u providers", providers->len);

        plugin_id = ldm_plugin_get_name(ldm_provider_get_plugin(providers->pdata[0]));
        fail_if(!g_str_equal(plugin_id, "nvidia-glx-driver"), "Failed to grab correct plugin");
}
END_TEST

/**
 * This test verifies the ability to get multiple candidates for a single device
 * and ensures GPUConfig returns the right detection device.
 *
 * Additionally it ensures we get the candidates in the right order.
 */
START_TEST(test_plugins_nvidia_multiple)
{
        g_autoptr(LdmManager) manager = NULL;
        autofree(UMockdevTestbed) *bed = NULL;
        g_autoptr(LdmGPUConfig) gpu = NULL;
        g_autoptr(GPtrArray) providers = NULL;
        const gchar *plugin_id = NULL;

        bed = create_bed_from(OPTIMUS_MOCKDEV_FILE);
        manager = ldm_manager_new(0);

        fail_if(!ldm_manager_add_modalias_plugin_for_path(manager, NV_340_MODALIAS),
                "Failed to add 340 modalias file");
        fail_if(!ldm_manager_add_modalias_plugin_for_path(manager, NV_MAIN_MODALIAS),
                "Failed to add main modalias file");

        gpu = ldm_gpu_config_new(manager);
        fail_if(!gpu, "Failed to create GPUConfig");

        providers = ldm_gpu_config_get_providers(gpu);
        fail_if(providers->len != 2, "Expected 2 providers, got %u providers", providers->len);

        plugin_id = ldm_plugin_get_name(ldm_provider_get_plugin(providers->pdata[0]));
        fail_if(!g_str_equal(plugin_id, "nvidia-glx-driver"),
                "First candidate should be nvidia-glx-driver, got %s",
                plugin_id);

        plugin_id = ldm_plugin_get_name(ldm_provider_get_plugin(providers->pdata[1]));
        fail_if(!g_str_equal(plugin_id, "nvidia-340-glx-driver"),
                "Second candidate should be nvidia-340-glx-driver, got %s",
                plugin_id);
}
END_TEST

/**
 * Identical to test_plugins_nvidia_multiple except we don't manually add
 * the plugins, we add them from the search path.
 */
START_TEST(test_plugins_nvidia_multiple_glob)
{
        g_autoptr(LdmManager) manager = NULL;
        autofree(UMockdevTestbed) *bed = NULL;
        g_autoptr(LdmGPUConfig) gpu = NULL;
        g_autoptr(GPtrArray) providers = NULL;
        const gchar *plugin_id = NULL;

        bed = create_bed_from(OPTIMUS_MOCKDEV_FILE);
        manager = ldm_manager_new(0);

        /* Modalias plugins preserve the priority from the insert order. */
        fail_if(!ldm_manager_add_modalias_plugins_for_directory(manager, MODALIAS_DIR),
                "Failed to add main modalias directory");

        gpu = ldm_gpu_config_new(manager);
        fail_if(!gpu, "Failed to create GPUConfig");

        providers = ldm_gpu_config_get_providers(gpu);
        fail_if(providers->len != 2, "Expected 2 provider, got %u providers", providers->len);

        plugin_id = ldm_plugin_get_name(ldm_provider_get_plugin(providers->pdata[0]));
        fail_if(!g_str_equal(plugin_id, "nvidia-glx-driver"),
                "First candidate should be nvidia-glx-driver, got %s",
                plugin_id);

        plugin_id = ldm_plugin_get_name(ldm_provider_get_plugin(providers->pdata[1]));
        fail_if(!g_str_equal(plugin_id, "nvidia-340-glx-driver"),
                "Second candidate should be nvidia-340-glx-driver, got %s",
                plugin_id);
}
END_TEST

/**
 * This test ensures we're able to identify `hid:` style modaliases on HID
 * devices in a USB device tree.
 *
 * Specifically we're trying to match the Razer Chroma Ornata device.
 */
START_TEST(test_plugins_razer)
{
        g_autoptr(LdmManager) manager = NULL;
        autofree(UMockdevTestbed) *bed = NULL;
        g_autoptr(GPtrArray) devices = NULL;
        g_autoptr(GPtrArray) providers = NULL;
        LdmDevice *device = NULL;
        const gchar *provider_id = NULL;

        bed = create_bed_from(RAZER_MOCKDEV_FILE);
        manager = ldm_manager_new(0);

        /* Modalias plugins preserve the priority from the insert order. */
        fail_if(!ldm_manager_add_modalias_plugins_for_directory(manager, MODALIAS_DIR),
                "Failed to add main modalias directory");

        devices = ldm_manager_get_devices(manager, LDM_DEVICE_TYPE_USB | LDM_DEVICE_TYPE_HID);
        fail_if(devices->len != 1, "Failed to find HID device!");

        device = devices->pdata[0];

        providers = ldm_manager_get_providers(manager, device);
        fail_if(providers->len != 1, "Expected 1 provider, got %u providers", providers->len);
        provider_id = ldm_provider_get_package(providers->pdata[0]);

        fail_if(!g_str_equal(provider_id, "razer-drivers"),
                "Expected 'razer-drivers', got '%s'",
                provider_id);
}
END_TEST

/**
 * Standard helper for running a test suite
 */
static int ldm_test_run(Suite *suite)
{
        SRunner *runner = NULL;
        int n_failed = 0;

        runner = srunner_create(suite);
        srunner_run_all(runner, CK_VERBOSE);
        n_failed = srunner_ntests_failed(runner);
        srunner_free(runner);

        return n_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static Suite *test_create(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create(__FILE__);
        tc = tcase_create(__FILE__);
        suite_add_tcase(s, tc);

        tcase_add_test(tc, test_plugins_nvidia);
        tcase_add_test(tc, test_plugins_nvidia_multiple);
        tcase_add_test(tc, test_plugins_nvidia_multiple_glob);
        tcase_add_test(tc, test_plugins_razer);

        return s;
}

int main(__ldm_unused__ int argc, __ldm_unused__ char **argv)
{
        return ldm_test_run(test_create());
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
