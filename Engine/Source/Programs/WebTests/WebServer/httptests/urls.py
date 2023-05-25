from django.urls import path

from . import views

urlpatterns = [
    path("httptests/methods", views.methods, name="methods"),
    path("httptests/get_large_response_without_chunks/<int:bytes_number>/", views.get_large_response_without_chunks, name="get_large_response_without_chunks"),
    path("httptests/nonstreaming_receivetimeout/<int:wait_time>/", views.nonstreaming_receivetimeout, name="nonstreaming_receivetimeout"),
    path("httptests/streaming_download/<int:chunks>/<int:chunk_size>/", views.streaming_download, name="streaming_download"),
    path("httptests/streaming_upload", views.streaming_upload, name="streaming_upload"),
]
