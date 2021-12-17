#!/bin/bash

curl http://127.0.0.1:14444/war --proxy http://localhost:7777 -v > war-b.txt

openssl sha256 war*.txt