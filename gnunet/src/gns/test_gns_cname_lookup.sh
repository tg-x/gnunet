#!/bin/bash
trap "gnunet-arm -e -c test_gns_lookup.conf" SIGINT
rm -r `gnunet-config -c test_gns_lookup.conf -s PATHS -o SERVICEHOME`
TEST_DOMAIN_PLUS="www.gnu"
TEST_DOMAIN_DNS="www3.gnu"
TEST_IP_PLUS="127.0.0.1"
TEST_IP_DNS="131.159.74.67"
TEST_RECORD_CNAME_SERVER="server"
TEST_RECORD_CNAME_PLUS="server.+"
TEST_RECORD_CNAME_DNS="gnunet.org"
TEST_RECORD_NAME_SERVER="server"
TEST_RECORD_NAME_PLUS="www"
TEST_RECORD_NAME_DNS="www3"

gnunet-arm -s -c test_gns_lookup.conf
gnunet-identity -C testego -c test_gns_lookup.conf
gnunet-namestore -p -z testego -a -n $TEST_RECORD_NAME_DNS -t CNAME -V $TEST_RECORD_CNAME_DNS -e never -c test_gns_lookup.conf
gnunet-namestore -p -z testego -a -n $TEST_RECORD_NAME_PLUS -t CNAME -V $TEST_RECORD_CNAME_PLUS -e never -c test_gns_lookup.conf
gnunet-namestore -p -z testego -a -n $TEST_RECORD_CNAME_SERVER -t A -V $TEST_IP_PLUS -e never -c test_gns_lookup.conf
RES_CNAME=$(timeout 5 gnunet-gns --raw -z testego -u www.gnu -t A -c test_gns_lookup.conf)
RES_CNAME_DNS=$(timeout 5 gnunet-gns --raw -z testego -u www3.gnu -t A -c test_gns_lookup.conf)
gnunet-namestore -p -z testego -d -n $TEST_RECORD_NAME_DNS -t CNAME -V $TEST_RECORD_CNAME_DNS -e never -c test_gns_lookup.conf
gnunet-namestore -p -z testego -d -n $TEST_RECORD_NAME_PLUS -t CNAME -V $TEST_RECORD_CNAME_PLUS -e never -c test_gns_lookup.conf
gnunet-namestore -p -z testego -d -n $TEST_RECORD_CNAME_SERVER -t A -V $TEST_IP_PLUS -e never -c test_gns_lookup.conf
gnunet-identity -D testego -c test_gns_lookup.conf
gnunet-arm -e -c test_gns_lookup.conf

if [ "$RES_CNAME" == "$TEST_IP_PLUS" ]
then
  exit 0
else
  echo "Failed to resolve to proper IP, got $RES_CNAME."
  exit 1
fi

if [ "$RES_CNAME_DNS" == "$TEST_IP_DNS" ]
then
  exit 0
else
  echo "Failed to resolve to proper IP from DNS, got $RES_IP."
  exit 1
fi
