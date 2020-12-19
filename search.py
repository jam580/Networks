
from tornado.ioloop import IOLoop
import tornado.web
import tornado.websocket
import tornado.httpserver
import socket
import tornado.wsgi
import gzip
import zlib
import os
import sys
import ast

class MH(tornado.web.RequestHandler):
    def get(self):
        self.render("index.html")

    def post(self):
        includes = []
        testing = self.get_argument("title","")
        print("Holy we got", testing)
        querry = testing.split()
        #self.render("index.html")
        for subdir, dirs, files in os.walk('files'):
            for filename in files:
                filepath = subdir + os.sep + filename
                print("file is:",filename)
                #we dont want to open gz files first
                if filepath.endswith(".gz"):
                    continue 
                else:
                    #first grab the url
                    with open(filepath, 'r') as f:
                        url = f.readline()
                        hold = f.readline()
                        while hold !="sep\n":
                            print("Header field")
                            hold = f.readline()
                        alldata = f.read()
                        for word in querry:
                            if word in alldata:
                                includes.append(url)
                    confile = filepath[:-4] +".gz"
                    #now search through the contents of the url
                    try:
                        with gzip.open(confile, 'r') as fin:
                            content = fin.read()
                            alldata = content.decode("utf-8")
                            for word in querry:
                                if word in alldata:
                                    includes.append(url)
                    except:
                        print("file doesn't have gzip encoding")
        #at this point all files have been querried
        self.write('<!Doctype html>'
        '<html>'
            '<body>'
            '<P>Welcome to our Proxy Search Engine!</P>'
            '<p> please enter search terms for cached content</p>'
            '<p> words separated with spaces will be searched individually</p>'
            '<p> if word is found in cached file, it will be returned</p>'
            '<form method="POST" action="">'
            '<div style="margin-bottom:5px"></div>'
                '<input name="title" type="text"/>' 
            '</div>'
            
            '<div>'
                '<input type="submit"/>'
            '</div>'
            '</form>')
        for url in includes:
            self.write('<p><a href ="' +url+'">'+url+'</a></p>')
        self.write('</body>'
        '</html>')

                        
def makeapp():
    return tornado.web.Application([
        (r"/", MH),
    ])

def launch():
    app = makeapp()
    app.listen(8080)
    IOLoop.current().start()


def main():
    launch()


if __name__ == "__main__":
    main()