import urllib
import requests
import pandas as pd
from lxml import html
from lxml import etree
from bs4 import BeautifulSoup

# 연동정보
api_key = "GEIQI283-GEIQ-GEIQ-GEIQ-GEIQI283JQ"
service = "WFS"
version = "1.1.1"
request = "GetFeature"
typename = "A2SM_CRMNLSTATS"
# styles = "A2SM_CrmnlHspot_Tot_Brglr"
outputformat = "GML2"
base_url = "http://www.safemap.go.kr/sm/commonapis.do"
option = "?apikey=%s&SERVICE=%s&VERSION=%s&REQUEST=%s&SRS=EPSG&TYPENAME=%s&OUTPUTFORMAT=%s"
url = base_url + option % (api_key, service, version, request, typename, outputformat)

resp = requests.get(url)

resp.encoding
resp.encoding = 'utf-8'

resp.encoding

# text 추출
text = resp.text

echo text

soup = BeautifulSoup(text, 'lxml')
stats = soup.findAll('safemap:a2sm_crmnlstats')

header = [ ' objt_id ' , ' polc_nm ' , ' plcstn_nm ' , ' polc_se ' , ' murder ' , ' brglr ' , ' rape ' , ' theft ' , ' Viola ' , ' arson ' , ' nrctc ' , ' TMPTA ' , 'gamble','to ' , ' ctprvn_nm ' , ' sgg_kor_nm ' , ' ctprvn_cd ' , ' sgg_cd ' , ' x ' , ' y ' ]
body = [header]
for stat in stats:
    line = []
    line.append(stat.find('safemap:objt_id').text)
    line.append(stat.find('safemap:polc_nm').text)
    line.append(stat.find('safemap:plcstn_nm').text)
    line.append(stat.find('safemap:polc_se').text)
    line.append(stat.find('safemap:murder').text)
    line.append(stat.find('safemap:brglr').text)
    line.append(stat.find('safemap:rape').text)
    line.append(stat.find('safemap:theft').text)
    line.append(stat.find('safemap:violn').text)
    line.append(stat.find('safemap:arson').text)
    line.append(stat.find('safemap:nrctc').text)
    line.append(stat.find('safemap:tmpt').text)
    line.append(stat.find('safemap:gamble').text)
    line.append(stat.find('safemap:tot').text)
    line.append(stat.find('safemap:ctprvn_nm').text)
    line.append(stat.find('safemap:sgg_kor_nm').text)
    line.append(stat.find('safemap:ctprvn_cd').text)
    line.append(stat.find('safemap:sgg_cd').text)
    line.append(stat.find('safemap:x').text)
    line.append(stat.find('safemap:y').text)
    body.append(line)

df = pd.DataFrame(body)