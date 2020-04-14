#
# "main" pseudo-component makefile.
#
#
COMPONENT_ADD_INCLUDEDIRS := include

# Embed the server root certificate into the final binary
#
# (If this was a component, we would set COMPONENT_EMBED_TXTFILES here.)
COMPONENT_EMBED_TXTFILES := howsmyssl_com_root_cert.pem