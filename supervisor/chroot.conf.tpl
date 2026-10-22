; Example supervisor programs for a Debian chroot on Android.
; Deployed to /data/debian/etc/supervisor/conf.d/chroot.conf by install.sh
;
; Replace these examples with your own services, or keep your real config
; locally in config/services.conf (gitignored) and install.sh will deploy it
; instead.
;
; The process MUST run in the foreground (no self-daemonizing), otherwise
; supervisor treats it as exited.

[program:example-ssh]
command=/usr/sbin/sshd -D
priority=10
startsecs=2
autostart=true
autorestart=true
stdout_logfile=/var/log/supervisor/example-ssh.log
stdout_logfile_maxbytes=2MB
stdout_logfile_backups=1
redirect_stderr=true

[program:example-app]
command=/opt/example/bin/example --config /etc/example/config.yml
directory=/opt/example
priority=20
startsecs=5
autostart=true
autorestart=true
stdout_logfile=/var/log/supervisor/example-app.log
stdout_logfile_maxbytes=2MB
stdout_logfile_backups=1
redirect_stderr=true
; environment=KEY="value",KEY2="value2"
