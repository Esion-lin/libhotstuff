scriptdir=$(dirname "$(readlink -f "$0")")
docker run -it -p 10060:10060 -p 10061:10061 -p 10062:10062 -p 10063:10063 -p 10080:10080 -p 10081:10081 -p 10082:10082 -p 10083:10083 -v ./log_info:/hotstuff hotstuff:latest
