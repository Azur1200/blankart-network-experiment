# connection menu testing script
# "masterserver http://localhost:12345" in console
# i know nothing about HTTP

import http.server

# just enough for two pages...
fakeserverlist = """\
localhost 5029 Discord: Jeffma Balls#6942
localhost 5030 Discord: Jeffma Balls#6942
localhost 5031 Discord: Jeffma Balls#6942
localhost 5032 Discord: Jeffma Balls#6942
localhost 5033 Discord: Jeffma Balls#6942
localhost 5034 Discord: Jeffma Balls#6942
localhost 5035 Discord: Jeffma Balls#6942
localhost 5036 Discord: Jeffma Balls#6942
localhost 5037 Discord: Jeffma Balls#6942
localhost 5038 Discord: Jeffma Balls#6942
localhost 5039 Discord: Jeffma Balls#6942
localhost 5040 Discord: Jeffma Balls#6942
"""

class jart(http.server.BaseHTTPRequestHandler):
	def do_GET(self):
		response = ""
		if self.path.startswith("/games/SRB2Kart/version"):
			response = "11 the best version\n"
		elif self.path.startswith("/games/SRB2Kart/11/servers") or self.path.startswith("/games/SRB2Kart/10/servers"):
			response = fakeserverlist
		elif self.path.startswith("/rules"):
			response = "Do whatever lol\n\n"

		self.send_response(200)
		self.end_headers()
		if not response:
			print("No response!?")
		self.wfile.write(bytes(response, "utf-8"))

server = http.server.HTTPServer(("localhost", 12345), jart)
server.serve_forever()
