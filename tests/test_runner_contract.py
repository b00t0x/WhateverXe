#!/usr/bin/env python3
"""Check omissions, failure stages and output isolation in the real runner."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import test as runner


class RunnerContract(unittest.TestCase):
    def args(self):
        return argparse.Namespace(donor=None,accelerator=None,kernels={},host=[],hde_source=None,ccs_records=None,outer_source=None,linux_tables=None,pcode_donor=None)

    def test_every_registered_script_exists_and_names_are_unique(self):
        jobs = runner.make_jobs(self.args(),ROOT,Path('unused'))
        self.assertEqual(len(jobs),len({j.name for j in jobs}))
        for job in jobs:
            for stage,command in job.steps:
                if stage == 'script':
                    self.assertTrue(Path(command[1]).is_file(),job.name)
        # Every Python test has an explicit execution mode. These modules
        # are shared source/ABI helpers exercised by registered callers.
        scripts = {Path(c[1]).name for j in jobs for stage,c in j.steps if stage == 'script'}
        helpers = {'production_source.py','pmap_kernel_contract.py','compat_test_support.py'}
        self.assertEqual({p.name for p in (ROOT/'tests').glob('*.py')},scripts|helpers)
        native = next(j for j in jobs if j.name == 'tgl_gt2_urb_native')
        self.assertEqual(runner.missing_inputs(native,self.args()),['accelerator'])
        self.assertEqual(next(j for j in jobs if j.name == 'xelp_forcewake_irq').kind,'source-lint')
        self.assertEqual(next(j for j in jobs if j.name == 'host_power_native').requires,('host','hde_source'))
        # These used to overwrite each other's log and dict entry. Inventory
        # must retain the core state-machine test as well as the adapter test.
        for name in ['tgl_start_order','xelp_forcewake_irq']:
            self.assertIn(name,{j.name for j in jobs})
            self.assertIn(name+'_cpp',{j.name for j in jobs})
        cpp = [j for j in jobs if j.steps[0][0] == 'compile']
        self.assertEqual(len(cpp),sum(not p.stem.endswith('_fixture') for p in (ROOT/'tests').glob('*.cpp')))

    def test_missing_required_replay_is_failure_not_success(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            output = Path(tmp)/'run'
            rc = runner.main(['--only','tgl_gt2_urb_native','--require','tgl_gt2_urb_native','--output',str(output)])
            report = json.loads((output/'results.json').read_text())
            self.assertEqual(rc,1)
            self.assertEqual(report['jobs']['tgl_gt2_urb_native']['status'],'skipped')
            self.assertEqual(report['summary']['passed'],0)
            self.assertFalse(report['summary']['fully_executed'])

    def test_optional_replay_is_visible_even_without_inputs(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            output = Path(tmp)/'run'
            self.assertEqual(runner.main(['--only','host_power_native','--output',str(output)]),0)
            report = json.loads((output/'results.json').read_text())
            self.assertEqual(report['summary']['skipped']['host_power_native'],['host','hde_source'])
            self.assertFalse(report['summary']['fully_executed'])

    def test_failed_compile_never_runs_stale_executable(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp);sentinel = output/'stale-executed'
            job = runner.Job('broken','cpu',[
                ('compile',[sys.executable,'-c','raise SystemExit(7)']),
                ('execute',[sys.executable,'-c',f'open({str(sentinel)!r},"w").close()'])])
            _, result = runner.run_job(job,output)
            self.assertEqual(result['failed_stage'],'compile')
            self.assertEqual(result['stages'],[{'stage':'compile','returncode':7}])
            self.assertFalse(sentinel.exists())

    def test_execution_failure_is_distinguished(self):
        with tempfile.TemporaryDirectory() as tmp:
            _, result = runner.run_job(runner.Job('bad-run','cpu',[
                ('compile',[sys.executable,'-c','pass']),
                ('execute',[sys.executable,'-c','raise SystemExit(9)'])]),Path(tmp))
            self.assertEqual(result['failed_stage'],'execute')

    def test_existing_output_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp);marker = output/'results.json';marker.write_text('previous run')
            with self.assertRaises(FileExistsError), contextlib.redirect_stdout(io.StringIO()):
                runner.main(['--only','host_power_native','--output',str(output)])
            self.assertEqual(marker.read_text(),'previous run')

    def test_list_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()) as stdout:
            output = Path(tmp)/'absent'
            self.assertEqual(runner.main(['--list','--output',str(output)]),0)
            self.assertFalse(output.exists())
            jobs = json.loads(stdout.getvalue())['jobs']
            self.assertIn('tgl_gt2_urb_native',{j['name'] for j in jobs})


if __name__ == '__main__':
    unittest.main()
