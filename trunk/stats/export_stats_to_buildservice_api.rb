#!/usr/bin/env ruby


# this script generates a xml file from the 'redirect_stats'-table of the
# opensuse download redirector, and sends it to the opensuse build service api
# via http put request.


#------------------------------------
# <CONFIG>
#
# DB:
#
db_host  = 'localhost'
db_user  = 'root'
db_pass  = ''
db_name  = 'redirector'
db_table = 'redirect_stats'
#
# API:
#
api_user = 'Admin'
api_pass = 'opensuse'
api_url  = 'http://davey.suse.de:3001/statistics/redirect_stats'
#
# </CONFIG>
#------------------------------------


require 'uri'
require 'base64'
require 'net/http'
require 'tempfile'
require 'rubygems'
require_gem 'activesupport'
require_gem 'activerecord'


# connect to database
ActiveRecord::Base.establish_connection(
  :adapter  => 'mysql',
  :host     => db_host,
  :username => db_user,
  :password => db_pass,
  :database => db_name
)


# define model for statistics entries
class RedirectStats < ActiveRecord::Base ; end


# get all entries from database
begin
  db_stats = RedirectStats.find :all
rescue
  puts "ERROR while getting redirect_stats from database. Abort."
  exit false
end


# build nested hash with counters
stats = {}
db_stats.each do |s|
  stats[ s.project ] ||= {}
  stats[ s.project ][ s.package ] ||= {}
  stats[ s.project ][ s.package ][ s.repository ] ||= {}
  stats[ s.project ][ s.package ][ s.repository ][ s.arch ] ||= []
  stats[ s.project ][ s.package ][ s.repository ][ s.arch ] << s
end


# initialize xml-builder, send result to the xml_output variable
xml = Builder::XmlMarkup.new( :target => xml_output='', :indent => 2 )


# generate xml
xml.instruct!
xml.redirect_stats do
  stats.each_pair do |project_name, project|

    xml.project( :name => project_name ) do
      project.each_pair do |package_name, package|

        xml.package( :name => package_name ) do
          package.each_pair do |repo_name, repo|

            xml.repository( :name => repo_name ) do
              repo.each_pair do |arch_name, arch|

                xml.arch( :name => arch_name ) do
                  arch.each do |counter|

                    xml.count(
                      counter.count,
                      :filename   => counter.filename,
                      :filetype   => counter.filetype,
                      :version    => counter.version,
                      :release    => counter.release,
                      :created_at => counter.created_at.xmlschema,
                      :counted_at => counter.counted_at.xmlschema
                    )

                  end # each_counter

                end # 'arch' xml tag

              end # each_arch
            end # 'repository' xml tag

          end # each_repo
        end # 'package' xml tag

      end # each_package
    end # 'project' xml tag

  end # each_project
end # outer 'redirect_stats' xml tag


# write xml to file, for later inspection (for debugging only atm)
file = File.new( '/tmp/redirect_stats.xml', 'w+' )
file << xml_output
file.close
puts "XML written to #{file.path} ..."


# prepare api connection
url = URI.parse api_url
header = {}
header['Authorization'] = 'Basic ' + Base64.encode64("#{api_user}:#{api_pass}")


# send xml via put request to the build service api
puts "Send XML to build service API ..."
response = Net::HTTP.start url.host, url.port do |http|
  http.put url.path, xml_output, header
end
puts "===== API RESPONSE =====\n#{response}"

