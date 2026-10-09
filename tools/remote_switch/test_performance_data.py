import unittest
from performance_data import parse

class PerformanceDataTests(unittest.TestCase):
    def test_optional_pipeline_cpu(self):
        result=parse('[PERF] v=1 generation=1 active=1 buckets=1\n'
                     '[PERF-CALL] kind=vk-pipeline tid=4 caller=abc saved_eip=0 n=2 ns=9000000 max_ns=5000000 ge1ms=2 errors=0 cpu_n=2 cpu_ns=1000000\n')
        self.assertEqual(result['calls'][0]['cpu_ns'],1000000)
        self.assertTrue(result['calls'][0]['cpu_available'])
    def test_generation_and_hex(self):
        text = ('[PERF] v=1 generation=1 active=1 elapsed_ms=10 buckets=1 dropped=0\n'
                '[PERF-CALL] kind=qpc tid=4 caller=abc saved_eip=123 n=1 ns=900 max_ns=900 ge1ms=0 errors=0\n'
                '[PERF] v=1 generation=2 active=0 elapsed_ms=300100 buckets=9 dropped=3\n'
                '[PERF-CALL] kind=vk-fence tid=88 caller=def saved_eip=7bc00000 n=3 ns=6000000 max_ns=3000000 ge1ms=2 errors=0\n'
                '[PERF-CLOCK] module=cpu hz=0 result=00000123 source=clkrst-read\n')
        result = parse(text)
        self.assertEqual(result['generation'],2)
        self.assertEqual(len(result['calls']),1)
        self.assertEqual(result['calls'][0]['tid'],0x88)
        self.assertTrue(result['partial_or_top_only'])
        self.assertFalse(result['clocks']['cpu']['available'])
    def test_truncated_and_unrelated(self):
        self.assertEqual(parse('password=whatever\n[PERF-CALL] truncated\n'),{})
        result=parse('[PERF] v=1 generation=1 active=0 elapsed_ms=1 buckets=1 dropped=0\n[PERF-CALL] kind=qpc tid=bad\n')
        self.assertEqual(result['calls'],[])
        self.assertTrue(result['partial_or_top_only'])

if __name__ == '__main__': unittest.main()
