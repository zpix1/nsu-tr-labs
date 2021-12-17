from time import sleep
from flask import Flask
import config
from flask_cors import CORS

app = Flask(__name__)
CORS(app)
cors = CORS(app, resource={
    r"/*": {
        "origins": "*"
    }
})


@app.route('/err')
def err_route():
    sleep(5)
    return 'errr...', 500


@app.route("/sleep")
def sleep_route():
    sleep(5)
    return "<p>Awaken...</p>"


@app.route("/just_req")
def just_req():
    return "<p>MMM...</p>"


@app.route("/big")
def big():
    def generate():
        for _ in range(1024):
            sleep(0.03)
            yield 'A' * 1024 * 1024

    return app.response_class(generate())


@app.route("/war")
def war():
    text = open('war-test/war.txt', 'rb')

    def generate():
        i = 0
        for line in text:
            if i % 100 == 0:
                sleep(0.05)
            i += 1
            yield line

    return app.response_class(generate())


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=config.SERVER_PORT)
