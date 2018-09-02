SYSCONFDIR=/etc
SVDIR=$(SYSCONFDIR)/sv

SERVICES := $(shell find * -type d)

.PHONY: $(SERVICES)
$(SERVICES):
	$(eval TARGET := $(DESTDIR)$(SVDIR)/$@)
	install -d $(TARGET)
	install -m 0755 $@/run $@/finish -t $(TARGET)

install: $(SERVICES)
