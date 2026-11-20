from meshtastic.protobuf import admin_pb2, mesh_pb2

print("display_frame:", "display_frame" in mesh_pb2.FromRadio.DESCRIPTOR.fields_by_name)
print("get_display_frame_request:", "get_display_frame_request" in admin_pb2.AdminMessage.DESCRIPTOR.fields_by_name)
print("send_input_event:", "send_input_event" in admin_pb2.AdminMessage.DESCRIPTOR.fields_by_name)
