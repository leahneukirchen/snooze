DESTDIR=
SYSCONFDIR=/etc
SVDIR=$(SYSCONFDIR)/sv

install: FRC
	for f in daily hourly weekly monthly; do \
		mkdir -p $(DESTDIR)$(SVDIR)/snooze-$$f; \
		install -m0755 snooze-$$f/run snooze-$$f/finish \
			$(DESTDIR)$(SVDIR)/snooze-$$f/; \
	done

FRC:
