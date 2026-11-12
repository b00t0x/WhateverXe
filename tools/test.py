#!/usr/bin/env python3
"""Run CPU tests and enumerate optional binary replays without hiding omissions."""
import argparse
import concurrent.futures
from dataclasses import dataclass
import fnmatch
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
PYTHON_TESTS = """
tgl_aux_observer tgl_aux_parent tgl_ccs
tgl_gt2_urb framebuffer_training_registration framebuffer_common display13_shared_registration
profile_admission firmware_readout display13_offline adlp_fb_candidate display13_tc_native
display13_ddc adlp_load_only small_gt_urb display_backend_dispatch tgl_agdc_port_status
display_contract_adapter tgl_agdc_aux_observer xe_data_access adls_aux_parent adls_lane_power
adls_clock_guard adls_clock_runtime adls_boot_runtime xelp_mmio_bar bigsur_pmap tahoe_iosurface
tahoe_pmap sequoia_pmap sonoma_iosurface sonoma_pmap ventura_pmap monterey_pmap xelp_forcewake_irq
boot_arguments tgl_display_observers tgl_stream_ack tgl_transmitter_observer tgl_transition_live
tgl_aux_live tgl_bootstrap_host tgl_clock_order provider_binding tgl_start_order tgl_hdmi_wiring
tgl_bootstrap_aux tgl_copy_routing tgl_training_leases tgl_wiring tgl_boot_readout tgl_pcode_gate
tgl_port_use tgl_tc_cold tgl_fia tgl_port_map tgl_dp_alt_fia tgl_dp_mode_replace
tgl_phy_clock_gating tgl_probe dvmt compat_exports compat_producer host_publication tgl_power_fuses display13_boot_aux
display13_aux display13_sst display13_combo_clock adls_panel_pwm display13_panel_runtime
tgl_panel_timing backlight_adapter tgl_panel_power tgl_edp_transcoder tgl_training_pattern
tgl_combo_pll_write tgl_sink_framing tgl_combo_aux_gate tgl_combo_signal_guard
tgl_combo_aux_lifetime tgl_iosurface_compression tgl_panel_depth_route display13_tiling
display13_cursor_resize dsi_presentation dsi_native_mode dsi_backing_adapter dsi_update_dispatch
dsi_path_disposal dsi_clock dsi_quiesce_adapter dsi_quiesce dsi_surface dsi_ggtt
dsi_lifecycle_dispatch dsi_boot_adoption dsi_native_cache dsi_boot_probe dsi_resources
dsi_boot_adapter dsi_port_adapter dsi_apple_adapter fatal_log_storage boot_stack_limits
xelp_combo_clock_guard display13_tc_preparation display13_tc_disconnect display13_dbuf_setup
display13_retirement display13_trial_retirement display13_port_wait display13_agdc
display13_training_frontend framebuffer_donor_runtime compat_bundle_version dsi_backlight_adapter
dsi_restart dsi_panel dsi_command_adapter dsi_irq_live dsi_surface_adapter dsi_aperture_adapter
display13_shutdown display13_wake_notifications released_physical_admission released_topology
accelerator_resources display13_power_dispatch test_runner_contract
""".split()
FAMILIES = {'BigSur':'bigsur', 'Monterey':'monterey', 'Ventura':'ventura',
            'Sonoma':'sonoma', 'Sequoia':'sequoia', 'Tahoe':'tahoe'}
DONOR_TESTS = """tgl_aux_native tgl_boot_power tgl_disable_native display13_agdc display13_shutdown dsi_update_dispatch dsi_path_disposal
dsi_surface dsi_native_cache dsi_native_mode dsi_boot_probe dsi_port_adapter
dsi_apple_adapter dsi_apple_timing_native adls_ddi_power_native display13_tc_preparation
display13_retirement display13_port_wait framebuffer_training_registration
framebuffer_common tgl_hdmi_wiring framebuffer_donor_runtime display13_training_lease
display13_tc_native display13_ddc tgl_power_fuses display13_aux display13_sst
display13_combo_clock adls_panel_pwm display13_panel_runtime tgl_panel_timing
tgl_agdc_port_status tgl_tc_native tgl_tc_mode tgl_mode_native tgl_panel_depth_route
adls_donor adls_clock_native""".split()
ACCELERATOR_TESTS = """tgl_ccs tgl_completion_cache tgl_cps_color_pipe tgl_ff_dop
tgl_ff_mode tgl_object_preemption tgl_object_selector tgl_render_flush
dsi_backing_adapter dsi_backing_native tgl_rpcs_native small_gt_urb
tgl_tlb_preparser tgl_semaphore_tlb tgl_iosurface_compression tgl_gt2_urb""".split()

@dataclass
class Job:
    name: str
    kind: str
    steps: list
    requires: tuple = ()


def make_jobs(args, root, output):
    jobs = []
    for source in sorted((root/'tests').glob('*.cpp')):
        if source.stem.endswith('_fixture'):
            continue
        # Core C++ and adapter Python tests can share a stem. They are
        # different jobs: never overwrite either result or their logs.
        name = source.stem+'_cpp' if source.stem in PYTHON_TESTS else source.stem
        exe = output/name/'test'
        jobs.append(Job(name, 'cpu', [
            ('compile', ['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                         '-fno-omit-frame-pointer','-I'+str(root/'WhateverXe'),str(source),'-o',str(exe)]),
            ('execute', [str(exe)])]))
    for name in PYTHON_TESTS:
        kind = 'source-lint' if name == 'xelp_forcewake_irq' else 'cpu'
        jobs.append(Job(name, kind, [('script', [sys.executable,str(root/'tests'/(name+'.py'))])]))
    for name in DONOR_TESTS:
        command = [sys.executable,str(root/'tests'/(name+'.py'))]
        if args.donor:
            command.append(str(args.donor))
        label = name if name in {'dsi_apple_timing_native','adls_ddi_power_native'} else name+'_native'
        jobs.append(Job(label, 'binary-check', [('script', command)], ('donor',)))
    for name in ACCELERATOR_TESTS:
        command = [sys.executable,str(root/'tests'/(name+'.py'))]
        if args.accelerator:
            command.append(str(args.accelerator))
        label = name if name in {'dsi_backing_native','tgl_rpcs_native'} else name+'_native'
        jobs.append(Job(label, 'binary-check', [('script', command)], ('accelerator',)))
    # Replays with additional evidence or a distinct original input. Their
    # dependencies are visible even when these modes cannot be selected yet.
    for name, requirements, values in [
        ('tgl_ccs_alignment',('accelerator','ccs_records'),[args.accelerator,args.ccs_records]),
        ('tgl_outer_flush',('accelerator',),[args.accelerator,args.outer_source or root/'WhateverXe/kern_xe.cpp']),
        ('tgl_dkl_signal',('donor','linux_tables'),[args.donor,'--linux-tables',args.linux_tables]),
        ('tgl_pcode_edram',('pcode_donor',),[args.pcode_donor,'--audit-copy',output/'tgl_pcode_edram_native/audit-copy'])]:
        command = [sys.executable,str(root/'tests'/(name+'.py'))]+[str(v) for v in values if v is not None]
        jobs.append(Job(name+'_native','binary-check',[('script',command)],requirements))
    command = [sys.executable,str(root/'tests/xelp_forcewake_irq.py')]
    if args.accelerator:
        command += ['--donor',str(args.accelerator)]
    jobs.append(Job('xelp_forcewake_irq_native','binary-check',[('script',command)],('accelerator',)))
    for family, prefix in FAMILIES.items():
        command = [sys.executable,str(root/'tests'/(prefix+'_pmap.py'))]
        if family in args.kernels:
            command += ['--kernel',str(args.kernels[family])]
        if args.hde_source:
            command += ['--hde-source',str(args.hde_source)]
        jobs.append(Job(prefix+'_pmap_native','binary-check',[('script',command)],(family,'hde_source')))
    command = [sys.executable,str(root/'tests/host_power_contract.py')]
    if args.hde_source:
        command += ['--hde-source',str(args.hde_source)]
    for host in args.host:
        command += ['--host',str(host)]
    jobs.append(Job('host_power_native','binary-check',[('script',command)],('host','hde_source')))
    names = [job.name for job in jobs]
    if len(set(names)) != len(names):
        raise ValueError('duplicate test job names')
    return jobs


def missing_inputs(job, args):
    return [key for key in job.requires if not (args.kernels.get(key) if key in FAMILIES else getattr(args,key))]


def run_job(job, output):
    directory = output/job.name
    directory.mkdir()
    result = {'kind':job.kind,'requires':list(job.requires),'status':'passed','stages':[]}
    start = time.monotonic()
    with (directory/'test.log').open('w') as log:
        for stage, command in job.steps:
            log.write('STAGE '+stage+'\n');log.flush()
            try:
                rc = subprocess.run(command,stdout=log,stderr=log).returncode
            except OSError as error:
                log.write(str(error)+'\n');rc = 127
            result['stages'].append({'stage':stage,'returncode':rc})
            if rc:
                result.update(status='failed',failed_stage=stage)
                break
    result['seconds'] = round(time.monotonic()-start,3)
    return job.name, result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--donor',type=Path)
    parser.add_argument('--accelerator',type=Path)
    parser.add_argument('--kernel',action='append',default=[],metavar='FAMILY=PATH')
    parser.add_argument('--host',action='append',type=Path,default=[])
    parser.add_argument('--hde-source',type=Path)
    for option in ['ccs-records','outer-source','linux-tables','pcode-donor']:
        parser.add_argument('--'+option,type=Path)
    parser.add_argument('--list',action='store_true',help='list every job and missing input, without running')
    parser.add_argument('--only',action='append',default=[],metavar='GLOB',help='select jobs; repeatable')
    parser.add_argument('--require',action='append',default=[],metavar='GLOB',help='also select these jobs and fail if inputs are absent')
    parser.add_argument('--output',type=Path,help='new output directory; existing directories are never reused')
    parser.add_argument('--jobs',type=int,default=4)
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    args.kernels = {}
    for value in args.kernel:
        family, separator, path = value.partition('=')
        if not separator or family not in FAMILIES or family in args.kernels or not Path(path).is_file():
            parser.error('expected unique supported FAMILY=existing-kernel path')
        args.kernels[family] = Path(path).resolve()
    for key in ['donor','accelerator','hde_source','ccs_records','outer_source','linux_tables','pcode_donor']:
        value = getattr(args,key)
        if value:
            if not value.is_file():parser.error('--'+key.replace('_','-')+' requires a file')
            setattr(args,key,value.resolve())
    for host in args.host:
        if not host.is_dir():parser.error('--host requires a directory')
    args.host = [host.resolve() for host in args.host]
    planned = make_jobs(args,ROOT,Path('<output>'))
    for pattern in args.only+args.require:
        if not any(fnmatch.fnmatchcase(job.name,pattern) for job in planned):
            parser.error('no test job matches '+pattern)
    def required(job):return any(fnmatch.fnmatchcase(job.name,p) for p in args.require)
    def selected(job):return not args.only or required(job) or any(fnmatch.fnmatchcase(job.name,p) for p in args.only)
    if args.list:
        print(json.dumps({'jobs':[{'name':j.name,'kind':j.kind,'requires':list(j.requires),
              'missing':missing_inputs(j,args),'selected':selected(j),'required':required(j)} for j in planned]},indent=2))
        return 0
    if args.output:
        output = args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    else:
        parent = ROOT/'build/tests';parent.mkdir(parents=True,exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix='run-',dir=parent))
    results = {}
    runnable = []
    required_skips = []
    for job in make_jobs(args,ROOT,output):
        if not selected(job):
            results[job.name] = {'kind':job.kind,'status':'not_selected'}
        elif missing := missing_inputs(job,args):
            results[job.name] = {'kind':job.kind,'status':'skipped','missing':missing}
            if required(job):required_skips.append(job.name)
        else:
            runnable.append(job)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results.update(pool.map(lambda job:run_job(job,output),runnable))
    failed = [name for name,result in results.items() if result['status']=='failed']
    skipped = {name:result['missing'] for name,result in results.items() if result['status']=='skipped'}
    summary = {'passed':sum(r['status']=='passed' for r in results.values()),'failed':failed,
               'skipped':skipped,'required_skips':required_skips,
               'fully_executed':not skipped and not any(r['status']=='not_selected' for r in results.values()),
               'not_selected':sum(r['status']=='not_selected' for r in results.values())}
    report = {'schema':2,'summary':summary,'jobs':dict(sorted(results.items()))}
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(summary,output=str(output)),indent=2))
    return int(bool(failed or required_skips))


if __name__ == '__main__':
    sys.exit(main())
