#!/usr/bin/env python3
"""Run standalone CPU/mock tests; optional exact-donor replay."""
import argparse,concurrent.futures,json,subprocess,sys
from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--donor',type=Path);p.add_argument('--accelerator',type=Path);a=p.parse_args()
O=R/'build/tests';O.mkdir(parents=True,exist_ok=True)
python_tests=['tgl_gt2_urb','framebuffer_training_registration','framebuffer_common','display13_shared_registration','adlp_render','firmware_readout','display13_offline','adlp_fb_candidate','display13_tc_native','display13_ddc','adlp_load_only','small_gt_urb','display_backend_dispatch','tgl_agdc_port_status','display_contract_adapter','tgl_agdc_aux_observer','xe_data_access','adls_aux_parent','adls_lane_power','adls_clock_guard','adls_clock_runtime','adls_boot_runtime','xelp_mmio_bar','bigsur_pmap','tahoe_iosurface','tahoe_pmap','sequoia_pmap','sonoma_iosurface','sonoma_pmap','ventura_pmap','tgl_kernel_admission','monterey_pmap','xelp_forcewake_irq','boot_arguments','tgl_display_observers','tgl_stream_ack','tgl_transmitter_observer','tgl_transition_live','tgl_aux_live','tgl_bootstrap_host','tgl_clock_order','provider_binding','tgl_start_order','tgl_hdmi_wiring','tgl_bootstrap_aux','tgl_copy_routing','tgl_training_leases','tgl_wiring','tgl_boot_readout','tgl_pcode_gate','tgl_port_use','tgl_tc_cold','tgl_fia','tgl_port_map','tgl_dp_alt_fia','tgl_dp_mode_replace','tgl_phy_clock_gating','tgl_probe','dvmt','host_publication']
jobs=[(p.stem,['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(R/'WhateverXe'),str(p),'-o',str(O/p.stem)],[str(O/p.stem)]) for p in sorted((R/'tests').glob('*.cpp')) if not p.stem.endswith('_fixture')]
python_tests += ['tgl_power_fuses', 'display13_boot_aux', 'display13_aux', 'display13_sst', 'display13_combo_clock', 'adls_panel_pwm', 'display13_panel_runtime', 'tgl_panel_timing', 'backlight_adapter', 'tgl_panel_power', 'tgl_edp_transcoder', 'tgl_training_pattern', 'tgl_combo_pll_write', 'tgl_sink_framing', 'tgl_combo_aux_gate', 'tgl_combo_signal_guard', 'tgl_combo_aux_lifetime', 'tgl_iosurface_compression', 'tgl_panel_depth_route', 'display13_tiling']
python_tests += ['display13_cursor_resize','dsi_presentation','dsi_native_mode','dsi_backing_adapter','dsi_update_dispatch','dsi_path_disposal','dsi_clock','dsi_quiesce_adapter','dsi_quiesce','dsi_surface','dsi_ggtt','dsi_lifecycle_dispatch','dsi_boot_adoption','dsi_native_cache','dsi_boot_probe','dsi_resources','dsi_boot_adapter','dsi_port_adapter','dsi_apple_adapter','fatal_log_storage','boot_stack_limits','xelp_combo_clock_guard','display13_tc_preparation','display13_tc_disconnect','display13_dbuf_setup','display13_retirement','display13_trial_retirement','display13_port_wait','display13_agdc','display13_training_frontend','framebuffer_donor_runtime','compat_bundle_version']
python_tests += ['dsi_backlight_adapter','dsi_restart','dsi_panel','dsi_command_adapter','dsi_irq_live','dsi_surface_adapter','dsi_aperture_adapter']
python_tests += ['display13_shutdown']
jobs += [(n,[sys.executable,str(R/'tests'/(n+'.py'))],None) for n in python_tests]
if a.donor:jobs += [('display13_agdc_native',[sys.executable,str(R/'tests/display13_agdc.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('display13_shutdown_native',[sys.executable,str(R/'tests/display13_shutdown.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('dsi_update_dispatch_native',[sys.executable,str(R/'tests/dsi_update_dispatch.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('dsi_path_disposal_native',[sys.executable,str(R/'tests/dsi_path_disposal.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('dsi_surface_native',[sys.executable,str(R/'tests/dsi_surface.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('dsi_native_cache_native',[sys.executable,str(R/'tests/dsi_native_cache.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('dsi_native_mode_native',[sys.executable,str(R/'tests/dsi_native_mode.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('dsi_boot_probe_native',[sys.executable,str(R/'tests/dsi_boot_probe.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('dsi_port_adapter_native',[sys.executable,str(R/'tests/dsi_port_adapter.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('dsi_apple_adapter_native',[sys.executable,str(R/'tests/dsi_apple_adapter.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('dsi_apple_timing_native',[sys.executable,str(R/'tests/dsi_apple_timing_native.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [('adls_ddi_power_native',[sys.executable,str(R/'tests/adls_ddi_power_native.py'),str(a.donor.resolve())],None)]
if a.donor:jobs += [(n+'_native',[sys.executable,str(R/'tests'/(n+'.py')),str(a.donor.resolve())],None) for n in ['display13_tc_preparation','display13_retirement','display13_port_wait','framebuffer_training_registration','framebuffer_common','tgl_hdmi_wiring','framebuffer_donor_runtime','display13_training_lease','display13_tc_native','display13_ddc','tgl_power_fuses', 'display13_aux', 'display13_sst', 'display13_combo_clock', 'adls_panel_pwm', 'display13_panel_runtime', 'tgl_panel_timing','tgl_agdc_port_status','tgl_tc_native','tgl_tc_mode','tgl_mode_native','tgl_panel_depth_route','adls_donor','adls_clock_native']]
if a.accelerator:jobs += [('dsi_backing_adapter_native',[sys.executable,str(R/'tests/dsi_backing_adapter.py'),str(a.accelerator.resolve())],None)]
if a.accelerator:jobs += [('dsi_backing_native',[sys.executable,str(R/'tests/dsi_backing_native.py'),str(a.accelerator.resolve())],None)]
if a.accelerator:jobs += [('xelp_forcewake_irq_native',[sys.executable,str(R/'tests/xelp_forcewake_irq.py'),'--donor',str(a.accelerator.resolve())],None),('tgl_rpcs_native',[sys.executable,str(R/'tests/tgl_rpcs_native.py'),str(a.accelerator.resolve())],None)] + [(n+'_native',[sys.executable,str(R/'tests'/(n+'.py')),str(a.accelerator.resolve())],None) for n in ['small_gt_urb','tgl_tlb_preparser','tgl_semaphore_tlb','tgl_iosurface_compression']]
def check(job):
 n,compile,execute=job
 with (O/(n+'.log')).open('w') as log:
  rc=subprocess.run(compile,stdout=log,stderr=log).returncode
  if rc==0 and execute:rc=subprocess.run(execute,stdout=log,stderr=log).returncode
 return n,rc
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=dict(pool.map(check,jobs))
(O/'results.json').write_text(json.dumps(results,indent=2)+'\n')
failed={n:rc for n,rc in results.items() if rc};print(json.dumps({'passed':len(results)-len(failed),'failed':failed}))
sys.exit(bool(failed))
