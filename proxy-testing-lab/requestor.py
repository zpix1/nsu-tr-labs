import requests
import config
import time
import threading

def request(url):
    start = time.clock_gettime(time.CLOCK_MONOTONIC)
    r = requests.get(
        'http://localhost:' + str(config.PROXY_PORT) + url, 
        headers={
            'Host': 'localhost:' + str(config.SERVER_PORT)
        }
    )
    end = time.clock_gettime(time.CLOCK_MONOTONIC)
    print('{:.4} {} {}'.format(end - start, url, r.status_code))

def parallel(url1, url2):
    start = time.clock_gettime(time.CLOCK_MONOTONIC)
    a = threading.Thread(target=request, args=[url1])
    b = threading.Thread(target=request, args=[url2])
    a.start()
    b.start()
    a.join()
    b.join()
    end = time.clock_gettime(time.CLOCK_MONOTONIC)
    print('{:.4} total'.format(end - start))

def dudos(url):
    for i in range(2000):
        threading.Thread(target=request, args=[url]).start()

def test():
    print('Caches')
    for i in range(2):
        request('/sleep?v=1')
    print()

    print('Parallel caches (total ~ 5)')
    parallel('/sleep?v=2', '/sleep?v=2')

    print()
    print('Do not cache errors')
    for i in range(2):
        request('/err?v=1')

if __name__ == '__main__':
    dudos('/just_req')