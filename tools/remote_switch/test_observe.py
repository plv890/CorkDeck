import tempfile
from pathlib import Path
import unittest
from observe import summarize, fresh_fault, remote_unavailable


class ObservationTests(unittest.TestCase):
    def test_bounded_recovery_allows_transient_startup_timeouts(self):
        self.assertFalse(remote_unavailable(3, 10, 34, 120))
        self.assertFalse(remote_unavailable(20, 10, 129, 120))
        self.assertTrue(remote_unavailable(20, 10, 130, 120))
        self.assertFalse(remote_unavailable(0, None, 300, 120))
        self.assertFalse(remote_unavailable(2, 10, 300, 120))
        self.assertTrue(remote_unavailable(3, 10, 10, 0))
    def test_retained_fault_requires_advancing_console_sample(self):
        old={'tick':100}
        state={'fatal_kind':1}
        self.assertFalse(fresh_fault(old,None,state))
        self.assertFalse(fresh_fault(old,old,state))
        self.assertFalse(fresh_fault({'tick':90},old,state))
        self.assertTrue(fresh_fault({'tick':101},old,state))
        self.assertFalse(fresh_fault({'tick':101},old,{'fatal_kind':0}))
        self.assertTrue(fresh_fault({'tick':101},old,{'fatal_kind':7}))
    def test_missing_snapshot_is_unknown(self):
        self.assertEqual(summarize(Path('.'), [{'name': 'missing.log'}]),
                         {'snapshot_status': 'unknown'})

    def test_newest_console_tick_wins_over_old_sequence(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as directory:
            folder = Path(directory)
            (folder/'old.log').write_text('[STARTUP] stage=6 total_s=600\n', encoding='utf-8')
            (folder/'new.log').write_text(
                '[STARTUP] stage=3 total_s=1\n'
                '[STARTUP] stage=4 total_s=91 stage_s=90 graphics_presents=0 window_frames=2\n'
                '[PROGRESS] syscalls=124224 frames=0 heap_used_mb=927\n', encoding='utf-8')
            result = summarize(folder, [
                {'name':'old.log', 'snapshot':{'tick':100,'sequence':999}},
                {'name':'new.log', 'snapshot':{'tick':200,'sequence':10}}])
            self.assertEqual(result['startup']['stage'], 4)
            self.assertEqual(result['startup']['window_frames'], 2)
            self.assertEqual(result['progress']['frames'], 0)
            self.assertNotIn('game_verdict', result)

    def test_compact_state_survives_stdout_flood(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as directory:
            folder = Path(directory)
            (folder/'live.log').write_text(
                '[STARTUP] stage=5 total_s=10 graphics_presents=1\n'
                '[STDOUT] 250\n'
                '[REMOTE-STATE] active=1 stage=6 total_s=20 stage_s=5 graphics_successful_presents=120 window_frames=0 fatal_kind=2 fatal_age_s=1\n',
                encoding='utf-8')
            result = summarize(folder, [{'name':'live.log', 'snapshot':{'tick':200,'sequence':10}}])
            self.assertEqual(result['startup']['stage'], 6)
            self.assertEqual(result['startup']['graphics_successful_presents'], 120)
            self.assertEqual(result['runtime_state']['fatal_kind'],2)


if __name__ == '__main__':
    unittest.main()
