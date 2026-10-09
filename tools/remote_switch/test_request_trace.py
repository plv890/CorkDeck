import unittest
from request_trace import parse_row,latest_requests

LINE='[REQUEST-PENDING] token=7 role=c tid=4 handle=81ff req=29 name=select age_ms=5000 flags=0 timeout=9223372036854775807 words=1,34,0,0,0 caller=1000 dropped=0'

class RequestTraceTests(unittest.TestCase):
    def test_typed_schema_and_latest(self):
        row=parse_row(LINE)
        self.assertEqual(row['words'],[1,0x34,0,0,0])
        self.assertEqual(row['call_id'],7)
        rows=latest_requests((LINE+'\n'+LINE.replace('age_ms=5000','age_ms=6000')+'\n').encode())
        self.assertEqual(len(rows),1);self.assertEqual(rows[0]['age_ms'],6000)
    def test_secret_or_extra_text_rejected(self):
        for bad in [LINE+' password=secret',LINE.replace('token=7','token=secret'),
                    LINE.replace('name=select','name=select-cookie'),LINE.replace('role=c','role=x'),
                    LINE.replace('tid=4','tid=100000000'),LINE.replace('words=1,34','words=100000000,34')]:
            self.assertIsNone(parse_row(bad))
    def test_non_rows_not_returned(self):
        self.assertEqual(latest_requests(b'password=secret\n[STDOUT] hello\n'),[])

if __name__=='__main__':unittest.main()
