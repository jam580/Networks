
from tornado.ioloop import IOLoop
import tornado.web
import tornado.websocket
import tornado.httpserver
import socket
import tornado.wsgi
from http.server import BaseHTTPRequestHandler, HTTPServer
import time

class MH(tornado.web.RequestHandler):
    def get(self):
        self.render("index.html")

    def post(self):
        testing = self.get_argument("title","")
        print("Holy shit we got", testing)
        querry = testing.split()
        self.render("index.html")

def makeapp():
    return tornado.web.Application([
        (r"/", MH),
    ])

def launch():
    app = makeapp()
    app.listen(8080)
    IOLoop.current().start()


def hello():
    return 'Hellow all'

def main():
    launch()


if __name__ == "__main__":
    main()